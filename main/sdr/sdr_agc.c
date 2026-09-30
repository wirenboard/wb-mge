#include "sdr_agc.h"

// Sample word: I in bits 19..10, Q in bits 9..0, both signed 10-bit
#define SDR_AGC_SAMPLE_I_S                  10
#define SDR_AGC_SAMPLE_Q_S                  0
#define SDR_AGC_SAMPLE_M                    0x3ffU
#define SDR_AGC_SAMPLE_SIGN                 0x200       // Sign bit of a 10-bit sample

#define SDR_AGC_GAIN_INITIAL                40          // A fixed gain around 40 gave a usable picture on the bench
#define SDR_AGC_GAIN_MIN                    0
// Overload at 1/4 of the 512 full scale keeps ~12 dB of headroom for packet peaks; at 7/8 the AGC was
// measured to climb about 12 steps above the gain that gives a clean picture on the bench.
#define SDR_AGC_CLIP_LEVEL                  128
#define SDR_AGC_QUIET_LEVEL                 32          // 1/16 of full scale: peaks well below the overload level
#define SDR_AGC_CLIP_MIN_SAMPLES            4           // Fewer overloaded samples in a scan are a stray spike
#define SDR_AGC_ATTACK_STEP                 2           // Fast attack: clipping smears spurs over the whole band
#define SDR_AGC_RELEASE_STEP                1           // Slow release: one step at a time
#define SDR_AGC_RELEASE_SCANS               10          // Slow release: sporadic packets must not pump the gain

static int sdr_agc_gain = SDR_AGC_GAIN_INITIAL;
static unsigned sdr_agc_quiet_scans = 0;                // Quiet scans in a row since the last reset


// |sample| of the signed 10-bit sample at bit shift of the word
static uint32_t sdr_agc_magnitude(uint32_t word, unsigned shift)
{
    int32_t raw = (int32_t)((word >> shift) & SDR_AGC_SAMPLE_M);
    int32_t sample = (raw ^ SDR_AGC_SAMPLE_SIGN) - SDR_AGC_SAMPLE_SIGN;
    return (uint32_t)((sample < 0) ? (-sample) : sample);
}


void sdr_agc_measure_reset(sdr_agc_measure_t *measure)
{
    measure->clip_count = 0;
    measure->peak = 0;
}


void sdr_agc_measure_add(sdr_agc_measure_t *measure, const uint32_t *words, size_t word_count)
{
    for (size_t n = 0; n < word_count; n++) {
        uint32_t i_magnitude = sdr_agc_magnitude(words[n], SDR_AGC_SAMPLE_I_S);
        uint32_t q_magnitude = sdr_agc_magnitude(words[n], SDR_AGC_SAMPLE_Q_S);
        uint32_t magnitude = (i_magnitude > q_magnitude) ? i_magnitude : q_magnitude;
        if (magnitude >= SDR_AGC_CLIP_LEVEL) {
            measure->clip_count++;
        }
        if (magnitude > measure->peak) {
            measure->peak = magnitude;
        }
    }
}


int sdr_agc_get_gain(void)
{
    return sdr_agc_gain;
}


void sdr_agc_update(const sdr_agc_measure_t *measure, int gain_max)
{
    if (measure->clip_count >= SDR_AGC_CLIP_MIN_SAMPLES) {
        sdr_agc_gain -= SDR_AGC_ATTACK_STEP;
        if (sdr_agc_gain < SDR_AGC_GAIN_MIN) {
            sdr_agc_gain = SDR_AGC_GAIN_MIN;
        }
        sdr_agc_quiet_scans = 0;
    } else if (measure->peak < SDR_AGC_QUIET_LEVEL) {
        sdr_agc_quiet_scans++;
        if (sdr_agc_quiet_scans >= SDR_AGC_RELEASE_SCANS) {
            sdr_agc_gain += SDR_AGC_RELEASE_STEP;
            if (sdr_agc_gain > gain_max) {
                sdr_agc_gain = gain_max;
            }
            sdr_agc_quiet_scans = 0;
        }
    } else {
        sdr_agc_quiet_scans = 0;
    }
}
