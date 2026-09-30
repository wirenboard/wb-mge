#include "sdr_radio.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <heap_memory_layout.h>
#include <soc/soc.h>
#include <soc/dport_reg.h>
#include <soc/dport_access.h>

#define SDR_RADIO_INIT_CHANNEL              1

// The only frequencies the radio tunes to: the Wi-Fi channel centres set_chanfreq() knows, channels 1..13
// and 14. Anything else is refused - the RF frequency offset path was measured to land about 7 MHz above
// channel 1 whatever frequency is requested.
#define SDR_RADIO_CHAN_FIRST_MHZ            2412
#define SDR_RADIO_CHAN_LAST_MHZ             2472
#define SDR_RADIO_CHAN_STEP_MHZ             5
#define SDR_RADIO_CHAN_14_MHZ               2484
#define SDR_RADIO_CHANFREQ_MODE             0

#define SDR_RADIO_RX_GAIN_REG               0x3ff5c02c
#define SDR_RADIO_RX_GAIN_MAX_S             8
#define SDR_RADIO_RX_GAIN_MAX_V             0x7f
#define SDR_RADIO_RX_GAIN_FORCE             BIT(23)     // Fixed gain instead of AGC
#define SDR_RADIO_RX_GAIN_KEEP_M            0x7fffff
#define SDR_RADIO_RX_GAIN_VALUE_S           24

// Analog baseband filter on the internal RF I2C bus
#define SDR_RADIO_FILTER_I2C_BLOCK          0x67
#define SDR_RADIO_FILTER_I2C_HOST           1
#define SDR_RADIO_FILTER_REG_FIRST          1
#define SDR_RADIO_FILTER_REG_COUNT          2
#define SDR_RADIO_FILTER_DCAP_M             0x7f
#define SDR_RADIO_FILTER_DCAP               8           // Widest: flat within ~1.2 dB over +-30 MHz at 80 MS/s
#define SDR_RADIO_FILTER_SETTLE_US          10

#define SDR_RADIO_DUMP_CTRL_REG             0x60033d90
#define SDR_RADIO_DUMP_STATUS_REG           0x60033d94
#define SDR_RADIO_DUMP_BYTES_REG            0x60033dc4
#define SDR_RADIO_DUMP_CTRL_ENABLE          BIT(31)
#define SDR_RADIO_DUMP_CTRL_START           BIT(19)
#define SDR_RADIO_DUMP_CTRL_DONE            BIT(18)
#define SDR_RADIO_DUMP_STATUS_COUNT_M       0x7fff
#define SDR_RADIO_DUMP_BYTES_ORDER          0x03020100
#define SDR_RADIO_DUMP_MODE_SAMPLES         3           // DPORT_MAC_DUMP_MODE value for the sample dump
#define SDR_RADIO_DUMP_TIMEOUT_US           20000
#define SDR_RADIO_DUMP_STOP_SETTLE_US       10

// The dump engine writes here by hardware, so the region is kept out of the heap. The linker script
// sdr_memory_guard.ld keeps static data and IRAM (SRAM1 is also mapped as IRAM, backwards) out of it.
#define SDR_RADIO_DUMP_START                0x3ffe8000
#define SDR_RADIO_DUMP_END                  0x3fff8000
#define SDR_RADIO_DUMP_WORDS                ((SDR_RADIO_DUMP_END - SDR_RADIO_DUMP_START) / sizeof(uint32_t))
#define SDR_RADIO_DUMP_SENTINEL             0xa55aa55a


// PHY and ROM radio functions, not declared by any ESP-IDF header
extern void rom_pbus_workmode(void);
extern void rom_pbus_xpd_tx_off(void);
extern void rom_pbus_xpd_rx_on(int enable);
extern void rom_set_rxclk_en(int enable);
extern unsigned rom_chip_i2c_readReg(unsigned block, unsigned host_id, unsigned reg_add);
extern void rom_chip_i2c_writeReg(unsigned block, unsigned host_id, unsigned reg_add, unsigned data);
extern void set_chanfreq(unsigned mhz, unsigned mode);

SOC_RESERVE_MEMORY_REGION(SDR_RADIO_DUMP_START, SDR_RADIO_DUMP_END, sdr_rf_dump);

static const char *TAG = "sdr_radio";

static int sdr_radio_gain_max = 0;
static unsigned sdr_radio_tuned_mhz = 0;                // 0: not tuned by this module yet

// Active scan with the driver's default per-channel timing. Hidden networks occupy the band like any other.
static const wifi_scan_config_t sdr_radio_scan_config = {
    .show_hidden = true,
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
};


static void sdr_radio_prepare_rx(void)
{
    rom_pbus_workmode();
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_on(true);
    rom_set_rxclk_en(true);
}

static bool sdr_radio_is_channel_centre(unsigned freq_mhz)
{
    return (((freq_mhz >= SDR_RADIO_CHAN_FIRST_MHZ) && (freq_mhz <= SDR_RADIO_CHAN_LAST_MHZ) &&
             (((freq_mhz - SDR_RADIO_CHAN_FIRST_MHZ) % SDR_RADIO_CHAN_STEP_MHZ) == 0)) ||
            (freq_mhz == SDR_RADIO_CHAN_14_MHZ));
}

static void sdr_radio_tune(unsigned freq_mhz)
{
    if (freq_mhz == sdr_radio_tuned_mhz) {
        return;
    }

    set_chanfreq(freq_mhz, SDR_RADIO_CHANFREQ_MODE);
    sdr_radio_prepare_rx();
    sdr_radio_tuned_mhz = freq_mhz;
}

static void sdr_radio_apply_gain(int gain)
{
    uint32_t value = REG_READ(SDR_RADIO_RX_GAIN_REG);
    REG_WRITE(SDR_RADIO_RX_GAIN_REG, ((value & SDR_RADIO_RX_GAIN_KEEP_M) |
                                      ((uint32_t)gain << SDR_RADIO_RX_GAIN_VALUE_S) |
                                      SDR_RADIO_RX_GAIN_FORCE));
}

static void sdr_radio_filter_apply(unsigned saved[SDR_RADIO_FILTER_REG_COUNT])
{
    for (unsigned i = 0; i < SDR_RADIO_FILTER_REG_COUNT; i++) {
        unsigned reg = SDR_RADIO_FILTER_REG_FIRST + i;
        saved[i] = rom_chip_i2c_readReg(SDR_RADIO_FILTER_I2C_BLOCK, SDR_RADIO_FILTER_I2C_HOST, reg);
        rom_chip_i2c_writeReg(SDR_RADIO_FILTER_I2C_BLOCK, SDR_RADIO_FILTER_I2C_HOST, reg,
                              ((saved[i] & (~SDR_RADIO_FILTER_DCAP_M)) | SDR_RADIO_FILTER_DCAP));
    }
    esp_rom_delay_us(SDR_RADIO_FILTER_SETTLE_US);
}

static void sdr_radio_filter_restore(const unsigned saved[SDR_RADIO_FILTER_REG_COUNT])
{
    for (unsigned i = 0; i < SDR_RADIO_FILTER_REG_COUNT; i++) {
        rom_chip_i2c_writeReg(SDR_RADIO_FILTER_I2C_BLOCK, SDR_RADIO_FILTER_I2C_HOST, SDR_RADIO_FILTER_REG_FIRST + i,
                              saved[i]);
    }
}


esp_err_t sdr_radio_init(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to init WiFi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Failed to set WiFi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_NULL), TAG, "Failed to set WiFi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "Failed to disable WiFi power save");
    ESP_RETURN_ON_ERROR(esp_wifi_set_promiscuous(true), TAG, "Failed to enable promiscuous mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(SDR_RADIO_INIT_CHANNEL, WIFI_SECOND_CHAN_NONE), TAG,
                        "Failed to set WiFi channel");

    sdr_radio_prepare_rx();
    sdr_radio_gain_max = (int)((REG_READ(SDR_RADIO_RX_GAIN_REG) >> SDR_RADIO_RX_GAIN_MAX_S) &
                               SDR_RADIO_RX_GAIN_MAX_V);

    ESP_LOGI(TAG, "SDR radio ready, gain_max %d", sdr_radio_gain_max);
    return ESP_OK;
}


int sdr_radio_get_gain_max(void)
{
    return sdr_radio_gain_max;
}


esp_err_t sdr_radio_capture(unsigned freq_mhz, int gain, const uint32_t **words)
{
    ESP_RETURN_ON_FALSE(sdr_radio_is_channel_centre(freq_mhz), ESP_ERR_INVALID_ARG, TAG,
                        "%u MHz is not a Wi-Fi channel centre", freq_mhz);

    // Volatile: the dump engine writes this memory behind the compiler's back
    volatile uint32_t *dump = (volatile uint32_t *)SDR_RADIO_DUMP_START;
    unsigned saved_filter[SDR_RADIO_FILTER_REG_COUNT] = {0};

    sdr_radio_tune(freq_mhz);
    sdr_radio_apply_gain(gain);

    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, 0);
    for (size_t i = 0; i < SDR_RADIO_DUMP_WORDS; i++) {
        dump[i] = SDR_RADIO_DUMP_SENTINEL;
    }
    sdr_radio_filter_apply(saved_filter);

    uint32_t ahb_sel = DPORT_REG_READ(DPORT_IRAM_DRAM_AHB_SEL_REG);
    uint32_t byte_order = REG_READ(SDR_RADIO_DUMP_BYTES_REG);
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG, ((ahb_sel & (~DPORT_MAC_DUMP_MODE_M)) |
                                                  (SDR_RADIO_DUMP_MODE_SAMPLES << DPORT_MAC_DUMP_MODE_S)));
    REG_WRITE(SDR_RADIO_DUMP_BYTES_REG, SDR_RADIO_DUMP_BYTES_ORDER);

    // No clock select bit: the dump runs at 80 MS/s
    uint32_t ctrl = (SDR_RADIO_MAX_SAMPLES | SDR_RADIO_DUMP_CTRL_ENABLE);
    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, ctrl);
    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, (ctrl | SDR_RADIO_DUMP_CTRL_START));
    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, ctrl);

    int64_t deadline_us = esp_timer_get_time() + SDR_RADIO_DUMP_TIMEOUT_US;
    while (((REG_READ(SDR_RADIO_DUMP_CTRL_REG) & SDR_RADIO_DUMP_CTRL_DONE) == 0) &&
           (esp_timer_get_time() < deadline_us))
    {
        // Busy-wait: a full dump takes about 0.2 ms at 80 MS/s
    }

    uint32_t result = REG_READ(SDR_RADIO_DUMP_CTRL_REG);
    uint32_t status = REG_READ(SDR_RADIO_DUMP_STATUS_REG);
    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, 0);
    esp_rom_delay_us(SDR_RADIO_DUMP_STOP_SETTLE_US);
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG, ahb_sel);
    REG_WRITE(SDR_RADIO_DUMP_BYTES_REG, byte_order);
    sdr_radio_filter_restore(saved_filter);

    if (((result & SDR_RADIO_DUMP_CTRL_DONE) == 0) ||
        ((status & SDR_RADIO_DUMP_STATUS_COUNT_M) != SDR_RADIO_MAX_SAMPLES))
    {
        ESP_LOGW(TAG, "Capture timed out: ctrl 0x%08" PRIx32 ", status 0x%08" PRIx32, result, status);
        return ESP_ERR_TIMEOUT;
    }

    // Every sample word must have been written, and nothing past them
    for (size_t i = 0; i < SDR_RADIO_DUMP_WORDS; i++) {
        bool untouched = (dump[i] == SDR_RADIO_DUMP_SENTINEL);
        if (untouched == (i < SDR_RADIO_MAX_SAMPLES)) {
            ESP_LOGW(TAG, "Capture wrote unexpected memory at word %u", (unsigned)i);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    *words = (const uint32_t *)SDR_RADIO_DUMP_START;
    return ESP_OK;
}


esp_err_t sdr_radio_scan_networks(wifi_ap_record_t *records, uint16_t *count)
{
    // Captures leave a forced fixed gain behind; the Wi-Fi receiver needs its own AGC back to hear beacons
    // at any level. The next capture forces its gain again.
    REG_CLR_BIT(SDR_RADIO_RX_GAIN_REG, SDR_RADIO_RX_GAIN_FORCE);

    // An all-channel scan starts on channel 1, and the driver skips the retune when it believes it is already
    // there - which it does after init, while captures have left the PLL elsewhere (channel 13 after a sweep:
    // no channel 1 network was found while stock firmware saw four). Channel 1 makes the first slot right
    // whatever the driver believes.
    set_chanfreq(SDR_RADIO_CHAN_FIRST_MHZ, SDR_RADIO_CHANFREQ_MODE);

    // Scanning needs the station interface, which the NULL mode of raw capture does not have
    esp_err_t ret = esp_wifi_set_promiscuous(false);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (ret == ESP_OK) {
        ret = esp_wifi_scan_start(&sdr_radio_scan_config, true);
    }
    if (ret == ESP_OK) {
        ret = esp_wifi_scan_get_ap_records(count, records);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Network scan failed: %s", esp_err_to_name(ret));
    }

    // Back to raw capture whatever failed above. The scan hopped channels and reset the receive path,
    // so the next capture must run set_chanfreq() and sdr_radio_prepare_rx() again.
    esp_err_t mode_ret = esp_wifi_set_mode(WIFI_MODE_NULL);
    esp_err_t promiscuous_ret = esp_wifi_set_promiscuous(true);
    sdr_radio_tuned_mhz = 0;
    if ((mode_ret != ESP_OK) || (promiscuous_ret != ESP_OK)) {
        ESP_LOGE(TAG, "Failed to restore raw capture after the network scan: mode %s, promiscuous %s",
                 esp_err_to_name(mode_ret), esp_err_to_name(promiscuous_ret));
    }

    if (ret == ESP_OK) {
        ret = (mode_ret != ESP_OK) ? mode_ret : promiscuous_ret;
    }
    return ret;
}
