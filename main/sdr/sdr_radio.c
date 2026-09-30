#include "sdr_radio.h"
#include "array_size.h"

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
#include <soc/rtc.h>

#define SDR_RADIO_INIT_CHANNEL              1

// Frequencies set_chanfreq() tunes natively: Wi-Fi channels 1..13 and 14. Anything else is reached
// from channel 1 through the RF frequency offset.
#define SDR_RADIO_CHAN_FIRST_MHZ            2412
#define SDR_RADIO_CHAN_LAST_MHZ             2472
#define SDR_RADIO_CHAN_STEP_MHZ             5
#define SDR_RADIO_CHAN_14_MHZ               2484
#define SDR_RADIO_CHANFREQ_MODE             0
#define SDR_RADIO_FREQ_OFFSET               0

// Crystal selector of rom_set_rf_freq_offset()
#define SDR_RADIO_XTAL_SEL_40M              0
#define SDR_RADIO_XTAL_SEL_26M              1
#define SDR_RADIO_XTAL_SEL_24M              2

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
#define SDR_RADIO_FILTER_SETTLE_US          10

#define SDR_RADIO_DUMP_CTRL_REG             0x60033d90
#define SDR_RADIO_DUMP_STATUS_REG           0x60033d94
#define SDR_RADIO_DUMP_BYTES_REG            0x60033dc4
#define SDR_RADIO_DUMP_CTRL_ENABLE          BIT(31)
#define SDR_RADIO_DUMP_CTRL_START           BIT(19)
#define SDR_RADIO_DUMP_CTRL_DONE            BIT(18)
#define SDR_RADIO_DUMP_CTRL_CLK_16M         BIT(16)
#define SDR_RADIO_DUMP_CTRL_CLK_40M         BIT(15)
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

// Sample word: bits 19..10 hold I and bits 9..0 hold Q, both signed 10-bit. The top 8 bits of each
// are kept.
#define SDR_RADIO_SAMPLE_I_S                12
#define SDR_RADIO_SAMPLE_Q_S                2
#define SDR_RADIO_SAMPLE_BYTE_M             0xff


// PHY and ROM radio functions, not declared by any ESP-IDF header
extern void rom_pbus_workmode(void);
extern void rom_pbus_xpd_tx_off(void);
extern void rom_pbus_xpd_rx_on(int enable);
extern void rom_set_rxclk_en(int enable);
extern unsigned rom_chip_i2c_readReg(unsigned block, unsigned host_id, unsigned reg_add);
extern void rom_chip_i2c_writeReg(unsigned block, unsigned host_id, unsigned reg_add, unsigned data);
extern void set_chanfreq(unsigned mhz, unsigned mode);
extern void rom_set_rf_freq_offset(unsigned xtal_sel, unsigned mhz, int offset);

// Analog filter calibration: capacitor code for a bandwidth, ordered by falling bandwidth
typedef struct {
    unsigned dcap;
    unsigned mhz;
} sdr_radio_filter_cal_t;

static const sdr_radio_filter_cal_t sdr_radio_filter_cal[] = {
    {8, 67}, {12, 55}, {16, 48}, {24, 38}, {32, 32}, {48, 25}, {64, 20}, {80, 17}, {96, 15}, {112, 14}, {127, 12},
};

SOC_RESERVE_MEMORY_REGION(SDR_RADIO_DUMP_START, SDR_RADIO_DUMP_END, sdr_rf_dump);

static const char *TAG = "sdr_radio";

static int sdr_radio_gain_max = 0;
static unsigned sdr_radio_tuned_mhz = 0;                // 0: not tuned by this module yet


static void sdr_radio_prepare_rx(void)
{
    rom_pbus_workmode();
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_on(true);
    rom_set_rxclk_en(true);
}

static unsigned sdr_radio_xtal_sel(void)
{
    soc_xtal_freq_t xtal = rtc_clk_xtal_freq_get();
    if (xtal == SOC_XTAL_FREQ_26M) {
        return SDR_RADIO_XTAL_SEL_26M;
    }
    if (xtal == SOC_XTAL_FREQ_24M) {
        return SDR_RADIO_XTAL_SEL_24M;
    }
    return SDR_RADIO_XTAL_SEL_40M;
}

static void sdr_radio_tune(unsigned freq_mhz)
{
    if (freq_mhz == sdr_radio_tuned_mhz) {
        return;
    }

    bool on_channel = (((freq_mhz >= SDR_RADIO_CHAN_FIRST_MHZ) && (freq_mhz <= SDR_RADIO_CHAN_LAST_MHZ) &&
                        (((freq_mhz - SDR_RADIO_CHAN_FIRST_MHZ) % SDR_RADIO_CHAN_STEP_MHZ) == 0)) ||
                       (freq_mhz == SDR_RADIO_CHAN_14_MHZ));
    if (on_channel) {
        set_chanfreq(freq_mhz, SDR_RADIO_CHANFREQ_MODE);
    } else {
        set_chanfreq(SDR_RADIO_CHAN_FIRST_MHZ, SDR_RADIO_CHANFREQ_MODE);
        rom_set_rf_freq_offset(sdr_radio_xtal_sel(), freq_mhz, SDR_RADIO_FREQ_OFFSET);
    }
    sdr_radio_prepare_rx();
    sdr_radio_tuned_mhz = freq_mhz;
}

static void sdr_radio_apply_gain(int gain)
{
    if (gain == SDR_RADIO_GAIN_AGC) {
        REG_CLR_BIT(SDR_RADIO_RX_GAIN_REG, SDR_RADIO_RX_GAIN_FORCE);
    } else {
        uint32_t value = REG_READ(SDR_RADIO_RX_GAIN_REG);
        REG_WRITE(SDR_RADIO_RX_GAIN_REG, ((value & SDR_RADIO_RX_GAIN_KEEP_M) |
                                          ((uint32_t)gain << SDR_RADIO_RX_GAIN_VALUE_S) |
                                          SDR_RADIO_RX_GAIN_FORCE));
    }
}

// Linear interpolation between the two calibration points around bw_mhz, rounded to nearest
static unsigned sdr_radio_filter_dcap(unsigned bw_mhz)
{
    const size_t last = ARRAY_SIZE(sdr_radio_filter_cal) - 1;
    if (bw_mhz >= sdr_radio_filter_cal[0].mhz) {
        return sdr_radio_filter_cal[0].dcap;
    }
    if (bw_mhz <= sdr_radio_filter_cal[last].mhz) {
        return sdr_radio_filter_cal[last].dcap;
    }

    for (size_t i = 1; i <= last; i++) {
        if (bw_mhz >= sdr_radio_filter_cal[i].mhz) {
            const sdr_radio_filter_cal_t *wide = &sdr_radio_filter_cal[i - 1];
            const sdr_radio_filter_cal_t *narrow = &sdr_radio_filter_cal[i];
            unsigned span = wide->mhz - narrow->mhz;
            return (wide->dcap + ((((narrow->dcap - wide->dcap) * (wide->mhz - bw_mhz)) + (span / 2)) / span));
        }
    }
    return sdr_radio_filter_cal[last].dcap;                 // Unreachable: the last point is handled above
}

static void sdr_radio_filter_apply(unsigned bw_mhz, unsigned saved[SDR_RADIO_FILTER_REG_COUNT])
{
    unsigned dcap = sdr_radio_filter_dcap(bw_mhz);
    for (unsigned i = 0; i < SDR_RADIO_FILTER_REG_COUNT; i++) {
        unsigned reg = SDR_RADIO_FILTER_REG_FIRST + i;
        saved[i] = rom_chip_i2c_readReg(SDR_RADIO_FILTER_I2C_BLOCK, SDR_RADIO_FILTER_I2C_HOST, reg);
        rom_chip_i2c_writeReg(SDR_RADIO_FILTER_I2C_BLOCK, SDR_RADIO_FILTER_I2C_HOST, reg,
                              ((saved[i] & (~SDR_RADIO_FILTER_DCAP_M)) | dcap));
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
    REG_CLR_BIT(SDR_RADIO_RX_GAIN_REG, SDR_RADIO_RX_GAIN_FORCE);

    ESP_LOGI(TAG, "SDR radio ready, gain_max %d", sdr_radio_gain_max);
    return ESP_OK;
}


int sdr_radio_get_gain_max(void)
{
    return sdr_radio_gain_max;
}


esp_err_t sdr_radio_capture(unsigned freq_mhz, sdr_radio_rate_t rate, unsigned bw_mhz, int gain,
                            const uint8_t **iq)
{
    // Volatile: the dump engine writes this memory behind the compiler's back
    volatile uint32_t *dump = (volatile uint32_t *)SDR_RADIO_DUMP_START;
    bool set_filter = (bw_mhz != SDR_RADIO_BW_KEEP);
    unsigned saved_filter[SDR_RADIO_FILTER_REG_COUNT] = {0};

    sdr_radio_tune(freq_mhz);
    sdr_radio_apply_gain(gain);

    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, 0);
    for (size_t i = 0; i < SDR_RADIO_DUMP_WORDS; i++) {
        dump[i] = SDR_RADIO_DUMP_SENTINEL;
    }
    if (set_filter) {
        sdr_radio_filter_apply(bw_mhz, saved_filter);
    }

    uint32_t ahb_sel = DPORT_REG_READ(DPORT_IRAM_DRAM_AHB_SEL_REG);
    uint32_t byte_order = REG_READ(SDR_RADIO_DUMP_BYTES_REG);
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG, ((ahb_sel & (~DPORT_MAC_DUMP_MODE_M)) |
                                                  (SDR_RADIO_DUMP_MODE_SAMPLES << DPORT_MAC_DUMP_MODE_S)));
    REG_WRITE(SDR_RADIO_DUMP_BYTES_REG, SDR_RADIO_DUMP_BYTES_ORDER);

    uint32_t ctrl = (SDR_RADIO_MAX_SAMPLES | SDR_RADIO_DUMP_CTRL_ENABLE);
    if (rate == SDR_RADIO_RATE_16_MSPS) {
        ctrl |= SDR_RADIO_DUMP_CTRL_CLK_16M;
    } else if (rate == SDR_RADIO_RATE_40_MSPS) {
        ctrl |= SDR_RADIO_DUMP_CTRL_CLK_40M;
    }
    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, ctrl);
    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, (ctrl | SDR_RADIO_DUMP_CTRL_START));
    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, ctrl);

    int64_t deadline_us = esp_timer_get_time() + SDR_RADIO_DUMP_TIMEOUT_US;
    while (((REG_READ(SDR_RADIO_DUMP_CTRL_REG) & SDR_RADIO_DUMP_CTRL_DONE) == 0) &&
           (esp_timer_get_time() < deadline_us))
    {
        // Busy-wait: a full dump takes about a millisecond even at 16 MSPS
    }

    uint32_t result = REG_READ(SDR_RADIO_DUMP_CTRL_REG);
    uint32_t status = REG_READ(SDR_RADIO_DUMP_STATUS_REG);
    REG_WRITE(SDR_RADIO_DUMP_CTRL_REG, 0);
    esp_rom_delay_us(SDR_RADIO_DUMP_STOP_SETTLE_US);
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG, ahb_sel);
    REG_WRITE(SDR_RADIO_DUMP_BYTES_REG, byte_order);
    if (set_filter) {
        sdr_radio_filter_restore(saved_filter);
    }

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

    // In place: bytes 2j and 2j+1 never reach past sample word j, which has already been read
    uint8_t *packed = (uint8_t *)SDR_RADIO_DUMP_START;
    for (size_t j = 0; j < SDR_RADIO_MAX_SAMPLES; j++) {
        uint32_t word = dump[j];
        uint8_t *sample = &packed[j * SDR_RADIO_BYTES_PER_SAMPLE];
        sample[0] = (uint8_t)((word >> SDR_RADIO_SAMPLE_I_S) & SDR_RADIO_SAMPLE_BYTE_M);
        sample[1] = (uint8_t)((word >> SDR_RADIO_SAMPLE_Q_S) & SDR_RADIO_SAMPLE_BYTE_M);
    }

    *iq = packed;
    return ESP_OK;
}
