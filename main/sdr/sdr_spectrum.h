#pragma once

#include <stddef.h>
#include <stdint.h>

#define SDR_SPECTRUM_FFT_SIZE               1024

/**
 * @brief Averaged power spectrum of raw sample words, in dBFS
 *
 * Splits the words into whole blocks of SDR_SPECTRUM_FFT_SIZE, removes the mean of I and Q in each block,
 * applies a periodic Hann window and averages |FFT|^2 over the blocks. A full-scale complex tone centred
 * on a bin reads 0 dBFS. The DC bin, emptied by the mean removal, reports the mean of its neighbours.
 * Not reentrant: the work buffers are static.
 *
 * @param words Sample words: I in bits 19..10, Q in bits 9..0, both signed 10-bit
 * @param word_count Number of words, at least SDR_SPECTRUM_FFT_SIZE
 * @param first_bin First bin to write in fftshift order: bin j lies (j - SDR_SPECTRUM_FFT_SIZE / 2) bins
 *                  from the centre frequency
 * @param bin_count Bins to write; first_bin + bin_count must not exceed SDR_SPECTRUM_FFT_SIZE
 * @param db Out: bin_count values of round(dBFS), clamped to the int8 range
 */
void sdr_spectrum_compute_db(const uint32_t *words, size_t word_count, size_t first_bin, size_t bin_count,
                             int8_t *db);
