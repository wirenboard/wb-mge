#include "sdr_spectrum.h"

#include <math.h>
#include <stdbool.h>

// Sample word: I in bits 19..10, Q in bits 9..0, both signed 10-bit
#define SDR_SPECTRUM_SAMPLE_I_S             10
#define SDR_SPECTRUM_SAMPLE_Q_S             0
#define SDR_SPECTRUM_SAMPLE_M               0x3ffU
#define SDR_SPECTRUM_SAMPLE_SIGN            0x200       // Sign bit of a 10-bit sample
#define SDR_SPECTRUM_SAMPLE_FULL_SCALE      512.0f

#define SDR_SPECTRUM_HALF                   (SDR_SPECTRUM_FFT_SIZE / 2)
// sin(a) = cos(a - pi / 2): the sine of table index t is the cosine at index t - N / 4, modulo N
#define SDR_SPECTRUM_SIN_OFFSET             (SDR_SPECTRUM_FFT_SIZE - (SDR_SPECTRUM_FFT_SIZE / 4))
#define SDR_SPECTRUM_TWO_PI                 6.28318530717958647692f
#define SDR_SPECTRUM_HANN_COEF              0.5f        // w[n] = 0.5 - 0.5 * cos(2 * pi * n / N)
// A periodic Hann window sums to exactly N / 2
#define SDR_SPECTRUM_WINDOW_SUM             ((float)SDR_SPECTRUM_FFT_SIZE / 2.0f)
#define SDR_SPECTRUM_POWER_MIN              1e-12f
#define SDR_SPECTRUM_DB_PER_DECADE          10.0f

typedef struct {
    float re;
    float im;
} sdr_spectrum_complex_t;

// cos(2 * pi * n / N): the FFT twiddles (the sine through SDR_SPECTRUM_SIN_OFFSET) and the Hann window
static float sdr_spectrum_cos[SDR_SPECTRUM_FFT_SIZE];
static bool sdr_spectrum_cos_ready = false;
static sdr_spectrum_complex_t sdr_spectrum_work[SDR_SPECTRUM_FFT_SIZE];
static float sdr_spectrum_acc[SDR_SPECTRUM_FFT_SIZE];   // Sum of |X|^2 over the blocks, in FFT order


static void sdr_spectrum_init_cos(void)
{
    for (size_t n = 0; n < SDR_SPECTRUM_FFT_SIZE; n++) {
        sdr_spectrum_cos[n] = cosf((SDR_SPECTRUM_TWO_PI * (float)n) / (float)SDR_SPECTRUM_FFT_SIZE);
    }
    sdr_spectrum_cos_ready = true;
}

// Sign-extends the 10-bit sample at bit shift of the word and scales it to +-1
static float sdr_spectrum_sample(uint32_t word, unsigned shift)
{
    int32_t raw = (int32_t)((word >> shift) & SDR_SPECTRUM_SAMPLE_M);
    return ((float)((raw ^ SDR_SPECTRUM_SAMPLE_SIGN) - SDR_SPECTRUM_SAMPLE_SIGN) / SDR_SPECTRUM_SAMPLE_FULL_SCALE);
}

// Decodes one block into the work buffer, removes the mean of I and of Q and applies the Hann window
static void sdr_spectrum_load_block(const uint32_t *words)
{
    float sum_i = 0.0f;
    float sum_q = 0.0f;
    for (size_t n = 0; n < SDR_SPECTRUM_FFT_SIZE; n++) {
        sdr_spectrum_work[n].re = sdr_spectrum_sample(words[n], SDR_SPECTRUM_SAMPLE_I_S);
        sdr_spectrum_work[n].im = sdr_spectrum_sample(words[n], SDR_SPECTRUM_SAMPLE_Q_S);
        sum_i += sdr_spectrum_work[n].re;
        sum_q += sdr_spectrum_work[n].im;
    }

    float mean_i = sum_i / (float)SDR_SPECTRUM_FFT_SIZE;
    float mean_q = sum_q / (float)SDR_SPECTRUM_FFT_SIZE;
    for (size_t n = 0; n < SDR_SPECTRUM_FFT_SIZE; n++) {
        float window = SDR_SPECTRUM_HANN_COEF - (SDR_SPECTRUM_HANN_COEF * sdr_spectrum_cos[n]);
        sdr_spectrum_work[n].re = (sdr_spectrum_work[n].re - mean_i) * window;
        sdr_spectrum_work[n].im = (sdr_spectrum_work[n].im - mean_q) * window;
    }
}

static void sdr_spectrum_bit_reverse(void)
{
    size_t j = 0;
    for (size_t i = 1; i < SDR_SPECTRUM_FFT_SIZE; i++) {
        size_t bit = SDR_SPECTRUM_HALF;
        while ((j & bit) != 0) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            sdr_spectrum_complex_t swap = sdr_spectrum_work[i];
            sdr_spectrum_work[i] = sdr_spectrum_work[j];
            sdr_spectrum_work[j] = swap;
        }
    }
}

// Forward transform of the work buffer in place, X[k] = sum x[n] * exp(-2 * pi * i * k * n / N):
// iterative radix-2 decimation in time
static void sdr_spectrum_fft(void)
{
    sdr_spectrum_bit_reverse();
    for (size_t len = 2; len <= SDR_SPECTRUM_FFT_SIZE; len <<= 1) {
        size_t half = len / 2;
        size_t stride = SDR_SPECTRUM_FFT_SIZE / len;
        for (size_t k = 0; k < half; k++) {
            size_t t = k * stride;
            float w_re = sdr_spectrum_cos[t];
            float w_im = -sdr_spectrum_cos[(t + SDR_SPECTRUM_SIN_OFFSET) % SDR_SPECTRUM_FFT_SIZE];
            for (size_t start = 0; start < SDR_SPECTRUM_FFT_SIZE; start += len) {
                sdr_spectrum_complex_t *a = &sdr_spectrum_work[start + k];
                sdr_spectrum_complex_t *b = &sdr_spectrum_work[(start + k) + half];
                float t_re = (b->re * w_re) - (b->im * w_im);
                float t_im = (b->re * w_im) + (b->im * w_re);
                b->re = a->re - t_re;
                b->im = a->im - t_im;
                a->re += t_re;
                a->im += t_im;
            }
        }
    }
}

// FFT index of fftshift bin j
static size_t sdr_spectrum_fft_index(size_t j)
{
    return ((j + SDR_SPECTRUM_HALF) % SDR_SPECTRUM_FFT_SIZE);
}

static int8_t sdr_spectrum_to_db(float power)
{
    float db = SDR_SPECTRUM_DB_PER_DECADE * log10f(fmaxf(power, SDR_SPECTRUM_POWER_MIN));
    return (int8_t)lroundf(fminf(fmaxf(db, (float)INT8_MIN), (float)INT8_MAX));
}


void sdr_spectrum_compute_db(const uint32_t *words, size_t word_count, size_t first_bin, size_t bin_count,
                             int8_t *db)
{
    if (!sdr_spectrum_cos_ready) {
        sdr_spectrum_init_cos();
    }

    size_t blocks = word_count / SDR_SPECTRUM_FFT_SIZE;
    for (size_t k = 0; k < SDR_SPECTRUM_FFT_SIZE; k++) {
        sdr_spectrum_acc[k] = 0.0f;
    }
    for (size_t block = 0; block < blocks; block++) {
        sdr_spectrum_load_block(&words[block * SDR_SPECTRUM_FFT_SIZE]);
        sdr_spectrum_fft();
        for (size_t k = 0; k < SDR_SPECTRUM_FFT_SIZE; k++) {
            sdr_spectrum_acc[k] += ((sdr_spectrum_work[k].re * sdr_spectrum_work[k].re) +
                                    (sdr_spectrum_work[k].im * sdr_spectrum_work[k].im));
        }
    }

    float scale = 1.0f / (((float)blocks * SDR_SPECTRUM_WINDOW_SUM) * SDR_SPECTRUM_WINDOW_SUM);
    for (size_t i = 0; i < bin_count; i++) {
        size_t j = first_bin + i;
        float acc = sdr_spectrum_acc[sdr_spectrum_fft_index(j)];
        if (j == SDR_SPECTRUM_HALF) {
            // DC, emptied by the mean removal: the mean of its neighbours instead
            acc = (sdr_spectrum_acc[sdr_spectrum_fft_index(j - 1)] +
                   sdr_spectrum_acc[sdr_spectrum_fft_index(j + 1)]) / 2.0f;
        }
        db[i] = sdr_spectrum_to_db(acc * scale);
    }
}
