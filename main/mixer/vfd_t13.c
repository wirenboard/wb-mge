#include "vfd_t13.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mb_master.h"

static const char *TAG = "vfd_t13";

// The quiet line the drive wants before a frame, and the drain the multiple answers to an
// FC06 write need. Both are guard intervals, so both are MINIMUMS - a pause shorter than asked
// for is a frame the drive may not answer.
//
// Two roundings stand in the way of that, and the macro below undoes both. pdMS_TO_TICKS()
// rounds DOWN to whole ticks (at the 500 Hz the firmware runs at a tick is 2 ms, so 5 ms comes
// out as 2 ticks), and vTaskDelay(n) only guarantees n-1 whole tick periods, because the first
// tick boundary can arrive the moment after the call. So the tick count is rounded UP and one
// tick is added: 5 ms becomes 4 ticks, i.e. at least 6 ms of quiet line.
#define VFD_T13_FRAME_GAP_MS            5
#define VFD_T13_WRITE_SETTLE_MS         40

#define VFD_T13_MS_TO_TICKS_UP(ms)      (((uint32_t)(ms) * (uint32_t)configTICK_RATE_HZ + 999u) / 1000u)
#define VFD_T13_DELAY_MS(ms)            vTaskDelay((TickType_t)(VFD_T13_MS_TO_TICKS_UP(ms) + 1u))

// Every exchange goes through this: one failure counts up, one success clears the count.
// Capped at the threshold so a drive that has been gone for a day is not a saturating
// counter away from being reported back online.
static void vfd_t13_account(vfd_t13_t *vfd, esp_err_t err)
{
    if (err == ESP_OK) {
        vfd->fail_count = 0;
        return;
    }
    if (vfd->fail_count < VFD_T13_OFFLINE_THRESHOLD) {
        vfd->fail_count++;
    }
}

void vfd_t13_configure(vfd_t13_t *vfd, uint8_t port, uint8_t slave_id, uint16_t rev_code)
{
    if (vfd == NULL) {
        return;
    }

    if ((vfd->port != port) || (vfd->slave_id != slave_id)) {
        // A different drive on a different bus: whatever the old one was doing says nothing
        // about this one.
        vfd->fail_count = 0;
    }

    vfd->port = port;
    vfd->slave_id = slave_id;
    vfd->rev_code = rev_code;
}

esp_err_t vfd_t13_set_frequency(vfd_t13_t *vfd, uint16_t freq_dhz)
{
    if (vfd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (freq_dhz < VFD_T13_FREQ_MIN_DHZ) {
        freq_dhz = VFD_T13_FREQ_MIN_DHZ;
    } else if (freq_dhz > VFD_T13_FREQ_MAX_DHZ) {
        freq_dhz = VFD_T13_FREQ_MAX_DHZ;
    }

    VFD_T13_DELAY_MS(VFD_T13_FRAME_GAP_MS);
    esp_err_t err = mb_master_write_holding(vfd->port, vfd->slave_id, VFD_T13_REG_FREQ,
                                            freq_dhz);
    VFD_T13_DELAY_MS(VFD_T13_WRITE_SETTLE_MS);

    vfd_t13_account(vfd, err);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "frequency write failed: %s", esp_err_to_name(err));
    }
    return err;
}

uint16_t vfd_t13_command_code(const vfd_t13_t *vfd, vfd_t13_dir_t dir)
{
    switch (dir) {
    case VFD_T13_DIR_FORWARD:
        return VFD_T13_CMD_FORWARD;
    case VFD_T13_DIR_REVERSE:
        // Configuration, not a constant: which code means reverse depends on the revision.
        return (vfd != NULL) ? vfd->rev_code : VFD_T13_CMD_REVERSE_NEW;
    default:
        return VFD_T13_CMD_STOP;
    }
}

esp_err_t vfd_t13_send_command(vfd_t13_t *vfd, vfd_t13_dir_t dir)
{
    if (vfd == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((dir != VFD_T13_DIR_STOP) && (dir != VFD_T13_DIR_FORWARD) && (dir != VFD_T13_DIR_REVERSE)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t code = vfd_t13_command_code(vfd, dir);

    VFD_T13_DELAY_MS(VFD_T13_FRAME_GAP_MS);
    esp_err_t err = mb_master_write_holding(vfd->port, vfd->slave_id, VFD_T13_REG_COMMAND,
                                            code);
    VFD_T13_DELAY_MS(VFD_T13_WRITE_SETTLE_MS);

    vfd_t13_account(vfd, err);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "command write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t vfd_t13_read_telemetry(vfd_t13_t *vfd, vfd_t13_telemetry_t *out)
{
    if ((vfd == NULL) || (out == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t regs[VFD_T13_REG_TELEMETRY_COUNT] = {0};

    VFD_T13_DELAY_MS(VFD_T13_FRAME_GAP_MS);
    esp_err_t err = mb_master_read_holding(vfd->port, vfd->slave_id, VFD_T13_REG_TELEMETRY,
                                           VFD_T13_REG_TELEMETRY_COUNT, regs);

    vfd_t13_account(vfd, err);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "telemetry read failed: %s", esp_err_to_name(err));
        return err;
    }

    out->bus_voltage_dv = regs[0];
    out->current_ca = regs[1];
    // Register 10 is signed: below zero it comes back as a two's-complement 16-bit value,
    // which is what this cast reads it as.
    out->temp_c = (int16_t)regs[2];
    return ESP_OK;
}

esp_err_t vfd_t13_read_feedback(vfd_t13_t *vfd, vfd_t13_feedback_t *out)
{
    if ((vfd == NULL) || (out == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t regs[VFD_T13_REG_FEEDBACK_COUNT] = {0};

    VFD_T13_DELAY_MS(VFD_T13_FRAME_GAP_MS);
    esp_err_t err = mb_master_read_holding(vfd->port, vfd->slave_id, VFD_T13_REG_FEEDBACK,
                                           VFD_T13_REG_FEEDBACK_COUNT, regs);

    vfd_t13_account(vfd, err);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "feedback read failed: %s", esp_err_to_name(err));
        return err;
    }

    out->freq_dhz = regs[VFD_T13_REG_FREQ - VFD_T13_REG_FEEDBACK];
    out->command = regs[VFD_T13_REG_COMMAND - VFD_T13_REG_FEEDBACK];
    return ESP_OK;
}

bool vfd_t13_is_online(const vfd_t13_t *vfd)
{
    if (vfd == NULL) {
        return false;
    }
    return (vfd->fail_count < VFD_T13_OFFLINE_THRESHOLD);
}
