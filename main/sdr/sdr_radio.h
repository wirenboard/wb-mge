#pragma once

#include <stdint.h>
#include "esp_err.h"

#define SDR_RADIO_MAX_SAMPLES               16380       // Samples per capture
#define SDR_RADIO_BYTES_PER_SAMPLE          2           // int8 I, then int8 Q
#define SDR_RADIO_CAPTURE_BYTES             (SDR_RADIO_MAX_SAMPLES * SDR_RADIO_BYTES_PER_SAMPLE)

// Tuning attempt limits, as in upstream ESP-SDR - not the range the synthesizer is known to lock in
// (upstream reports roughly 2.2-2.7 GHz)
#define SDR_RADIO_FREQ_MIN_MHZ              100
#define SDR_RADIO_FREQ_MAX_MHZ              6000

#define SDR_RADIO_BW_MIN_MHZ                12
#define SDR_RADIO_BW_MAX_MHZ                67
#define SDR_RADIO_BW_KEEP                   0           // Leave the analog filter as it is

#define SDR_RADIO_GAIN_AGC                  (-1)        // Hardware AGC instead of a fixed gain

// Sample rate, numbered like upstream's clock selector; sdr_radio_capture() maps it to the DUMP_CTRL
// clock bits, the value itself is never written to hardware.
typedef enum {
    SDR_RADIO_RATE_16_MSPS = 0,
    SDR_RADIO_RATE_80_MSPS = 1,
    SDR_RADIO_RATE_40_MSPS = 2,
} sdr_radio_rate_t;

// Takes the radio over for raw-IQ capture: starts the Wi-Fi driver in NULL mode with promiscuous
// reception and reads the maximum manual gain. Needs the default event loop. Wi-Fi must not be used
// by anything else afterwards.
esp_err_t sdr_radio_init(void);

// Highest gain sdr_radio_capture() accepts; 0 until sdr_radio_init() succeeded.
int sdr_radio_get_gain_max(void);

/**
 * @brief Capture SDR_RADIO_MAX_SAMPLES raw IQ samples
 * @param freq_mhz Centre frequency, SDR_RADIO_FREQ_MIN_MHZ..SDR_RADIO_FREQ_MAX_MHZ
 * @param rate Sample rate
 * @param bw_mhz Analog filter bandwidth, SDR_RADIO_BW_MIN_MHZ..SDR_RADIO_BW_MAX_MHZ, or SDR_RADIO_BW_KEEP
 * @param gain Fixed gain 0..sdr_radio_get_gain_max(), or SDR_RADIO_GAIN_AGC
 * @param iq On success, SDR_RADIO_CAPTURE_BYTES bytes: int8 I then int8 Q per sample. The buffer is
 *           reused by the next capture, and captures are not thread-safe.
 * @return ESP_OK, ESP_ERR_TIMEOUT if the dump did not complete, or ESP_ERR_INVALID_RESPONSE if it
 *         wrote a different region of memory than expected
 */
esp_err_t sdr_radio_capture(unsigned freq_mhz, sdr_radio_rate_t rate, unsigned bw_mhz, int gain,
                            const uint8_t **iq);
