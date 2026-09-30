#pragma once

#include <stdint.h>
#include "esp_err.h"

#define SDR_RADIO_MAX_SAMPLES               16380       // Samples per capture
#define SDR_RADIO_SAMPLE_RATE_KHZ           80000       // Every capture runs at 80 MS/s

#define SDR_RADIO_GAIN_AGC                  (-1)        // Hardware AGC instead of a fixed gain

// Takes the radio over for raw-IQ capture: starts the Wi-Fi driver in NULL mode with promiscuous
// reception and reads the maximum manual gain. Needs the default event loop. Wi-Fi must not be used
// by anything else afterwards.
esp_err_t sdr_radio_init(void);

// Highest gain sdr_radio_capture() accepts; 0 until sdr_radio_init() succeeded.
int sdr_radio_get_gain_max(void);

/**
 * @brief Capture SDR_RADIO_MAX_SAMPLES raw IQ samples at SDR_RADIO_SAMPLE_RATE_KHZ through the widest
 *        analog filter
 * @param freq_mhz Centre frequency: a Wi-Fi channel centre, 2412..2472 MHz in 5 MHz steps or 2484 MHz
 * @param gain Fixed gain 0..sdr_radio_get_gain_max(), or SDR_RADIO_GAIN_AGC
 * @param words On success, SDR_RADIO_MAX_SAMPLES sample words: I in bits 19..10, Q in bits 9..0, both
 *              signed 10-bit. They are overwritten by the next capture, and captures are not thread-safe.
 * @return ESP_OK, ESP_ERR_INVALID_ARG if freq_mhz is not a channel centre, ESP_ERR_TIMEOUT if the dump
 *         did not complete, or ESP_ERR_INVALID_RESPONSE if it wrote a different region of memory than expected
 */
esp_err_t sdr_radio_capture(unsigned freq_mhz, int gain, const uint32_t **words);
