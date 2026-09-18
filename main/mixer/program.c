#include "program.h"

#include <string.h>

#include "mbedtls/base64.h"

// mbedtls_base64_decode() answers MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL for anything longer
// than the buffer, which is how a twelfth step is caught: 12 steps decode to 48 bytes and
// the buffer below holds 44. There is deliberately no larger scratch buffer that would let
// an over-long program be decoded first and rejected afterwards - the ceiling is the point.
esp_err_t mixer_program_decode(const char *b64, mixer_program_t *out)
{
    if ((b64 == NULL) || (out == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    size_t b64_len = strlen(b64);
    if (b64_len == 0) {
        // The empty program: a device with nothing programmed yet, and the factory default.
        return ESP_OK;
    }

    uint8_t raw[MIXER_PROGRAM_MAX_BYTES] = {0};
    size_t raw_len = 0;
    int ret = mbedtls_base64_decode(raw, sizeof(raw), &raw_len,
                                    (const unsigned char *)b64, b64_len);
    if (ret != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((raw_len == 0) || ((raw_len % MIXER_STEP_WIRE_SIZE) != 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t count = raw_len / MIXER_STEP_WIRE_SIZE;

    for (size_t i = 0; i < count; i++) {
        const uint8_t *p = &raw[i * MIXER_STEP_WIRE_SIZE];
        mixer_step_t step = {
            .type       = p[0],
            .speed      = p[1],
            .duration_s = (uint16_t)(((uint16_t)p[2] << 8) | (uint16_t)p[3]),
        };

        if (step.type > MIXER_STEP_REVERSE) {
            memset(out, 0, sizeof(*out));
            return ESP_ERR_INVALID_ARG;
        }
        if (step.duration_s == 0) {
            memset(out, 0, sizeof(*out));
            return ESP_ERR_INVALID_ARG;
        }
        // A step that turns the shaft at no speed is not a way to stand still: the engine
        // raises whatever it converts to up to the drive's 1.0 Hz floor, so the motor would
        // turn slowly instead of not at all. Standing still is what a pause step is for, and
        // that is the only type the speed byte may be zero on.
        if ((step.type != MIXER_STEP_PAUSE) && (step.speed == 0)) {
            memset(out, 0, sizeof(*out));
            return ESP_ERR_INVALID_ARG;
        }

        out->steps[i] = step;
    }

    out->count = (uint8_t)count;
    return ESP_OK;
}

esp_err_t mixer_program_encode(const mixer_program_t *program, char *out, size_t out_size)
{
    if ((program == NULL) || (out == NULL) || (out_size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (program->count > MIXER_PROGRAM_MAX_STEPS) {
        return ESP_ERR_INVALID_ARG;
    }

    if (program->count == 0) {
        out[0] = '\0';
        return ESP_OK;
    }

    uint8_t raw[MIXER_PROGRAM_MAX_BYTES] = {0};
    for (size_t i = 0; i < program->count; i++) {
        const mixer_step_t *step = &program->steps[i];

        // The same three rules the decoder applies, from the other side: a type that exists, a
        // duration, and a speed on everything that is not a pause.
        if ((step->type > MIXER_STEP_REVERSE) || (step->duration_s == 0) ||
            ((step->type != MIXER_STEP_PAUSE) && (step->speed == 0))) {
            return ESP_ERR_INVALID_ARG;
        }

        uint8_t *p = &raw[i * MIXER_STEP_WIRE_SIZE];
        p[0] = step->type;
        p[1] = step->speed;
        p[2] = (uint8_t)(step->duration_s >> 8);
        p[3] = (uint8_t)(step->duration_s & 0xFF);
    }

    size_t raw_len = (size_t)program->count * MIXER_STEP_WIRE_SIZE;
    size_t written = 0;
    int ret = mbedtls_base64_encode((unsigned char *)out, out_size, &written, raw, raw_len);
    if (ret != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

bool mixer_program_is_valid(const char *b64)
{
    mixer_program_t program;
    return (mixer_program_decode(b64, &program) == ESP_OK);
}
