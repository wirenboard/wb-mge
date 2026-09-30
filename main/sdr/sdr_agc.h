#pragma once

#include <stddef.h>
#include <stdint.h>

// Signal level of one auto scan, gathered over all of its captures
typedef struct {
    uint32_t clip_count;                                // Samples with |I| or |Q| at the clip level or above
    uint32_t peak;                                      // Largest |I| or |Q| of any sample
} sdr_agc_measure_t;

// Empties a measurement before the first capture of a scan
void sdr_agc_measure_reset(sdr_agc_measure_t *measure);

/**
 * @brief Adds the samples of one capture to a scan's measurement
 * @param measure Measurement of the scan the capture belongs to
 * @param words Sample words: I in bits 19..10, Q in bits 9..0, both signed 10-bit
 * @param word_count Number of words
 */
void sdr_agc_measure_add(sdr_agc_measure_t *measure, const uint32_t *words, size_t word_count);

// Gain for the next auto scan; every capture of that scan must use it
int sdr_agc_get_gain(void);

/**
 * @brief Adjusts the gain after an auto scan: down fast on clipping, up slowly after a run of quiet scans
 * @param measure Measurement of the whole scan, taken with the gain sdr_agc_get_gain() returned
 * @param gain_max Highest gain a raise may reach
 */
void sdr_agc_update(const sdr_agc_measure_t *measure, int gain_max);
