#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "fp_lock.h"
#include "fp_log.h"
#include "fp_store.h"
#include "r503.h"

static const char *TAG = "fp_acs";

#define FP_ACS_TASK_STACK_SIZE          (4 * 1024)
#define FP_ACS_TASK_PRIORITY            4
#define FP_ACS_QUEUE_LENGTH             8

#define FP_ACS_POLL_MS                  100     // GenImg period while waiting for a finger or its lift
#define FP_ACS_RECONNECT_MS             3000    // handshake period while the sensor is offline
#define FP_ACS_BOOT_ATTEMPTS            3       // handshakes at boot before the sensor counts as offline
#define FP_ACS_BOOT_RETRY_MS            1000
#define FP_ACS_OFFLINE_ERRORS           3       // transport failures in a row that take the sensor offline
#define FP_ACS_ERROR_LED_MS             2000    // how long a failed enrollment shows solid red
#define FP_ACS_ENROLL_PLACE_TIMEOUT_MS  30000   // an enrollment take with no finger for this long fails
#define FP_ACS_ENROLL_ERROR_TIMEOUT     "timeout"
#define FP_ACS_SCAN_PASSES              5       // image-and-search passes before a finger counts as unknown

#define FP_ACS_SCAN_BUFFER              1       // feature buffer a scan is converted into and searched from
#define FP_ACS_TEMPLATE_BUFFER          1       // RegModel leaves the template here, Store reads it
#define FP_ACS_SEARCH_START             0
#define FP_ACS_LED_SPEED                25
#define FP_ACS_LED_FOREVER              0       // flash until the next LED command
#define FP_ACS_NIBBLE_BITS              4
#define FP_ACS_NIBBLE_MASK              0x0F
#define FP_ACS_NIBBLE_SCALE             17      // 4-bit grey 0..15 onto 8-bit 0..255
#define FP_ACS_US_PER_S                 1000000
#define FP_ACS_UTF8_CONT_MASK           0xC0    // a UTF-8 continuation byte is 10xxxxxx
#define FP_ACS_UTF8_CONT_BITS           0x80

typedef enum {
    FP_ACS_CMD_ENROLL,
    FP_ACS_CMD_DELETE_USER,
    FP_ACS_CMD_DELETE_FINGER,
} fp_acs_cmd_type_t;

typedef struct {
    fp_acs_cmd_type_t type;
    uint8_t           user_id;
    uint16_t          slot;
} fp_acs_cmd_t;

typedef enum {
    FP_ACS_WAIT_DONE,
    FP_ACS_WAIT_CANCELLED,
    FP_ACS_WAIT_OFFLINE,
    FP_ACS_WAIT_TIMEOUT,
} fp_acs_wait_t;

static QueueHandle_t     fp_acs_queue;

// Guards everything in this block: written by the sensor task (and by the request functions,
// for the enrollment it starts), read by any task.
static SemaphoreHandle_t fp_acs_mutex;
static bool              fp_acs_online;
static uint16_t          fp_acs_capacity;
static fp_acs_enroll_t   fp_acs_enroll;
static bool              fp_acs_cancel_requested;
static fp_acs_event_t    fp_acs_last_event;             // seq 0: no event yet
static uint8_t          *fp_acs_image;                  // the published last image
static uint32_t          fp_acs_image_seq;              // 0: no image yet
static uint32_t          fp_acs_image_event_seq;        // the event the image was taken for; 0: an enrollment take

// Sensor task only.
static uint8_t          *fp_acs_image_scratch;          // where the next image is received
static unsigned          fp_acs_transport_errors;
static bool              fp_acs_awaiting_lift;          // a scan was decided; the finger has not left yet
static bool              fp_acs_store_ok;


static void fp_acs_lock(void)
{
    xSemaphoreTake(fp_acs_mutex, portMAX_DELAY);
}

static void fp_acs_unlock(void)
{
    xSemaphoreGive(fp_acs_mutex);
}

static bool fp_acs_enroll_active_locked(void)
{
    return ((fp_acs_enroll.state == FP_ACS_ENROLL_STATE_PLACE) || (fp_acs_enroll.state == FP_ACS_ENROLL_STATE_LIFT) ||
            (fp_acs_enroll.state == FP_ACS_ENROLL_STATE_PROCESSING));
}

static void fp_acs_set_online(bool online)
{
    fp_acs_lock();
    bool changed = (fp_acs_online != online);
    fp_acs_online = online;
    fp_acs_unlock();
    if (changed && (!online)) {
        ESP_LOGE(TAG, "Sensor offline; retrying every %d ms", FP_ACS_RECONNECT_MS);
    }
}

// What a failed exchange is reported as: the driver's reason says what went wrong on the wire, the
// error name alone does not.
static const char *fp_acs_io_error_str(esp_err_t err)
{
    const char *reason = r503_last_error();
    return ((reason[0] != '\0') ? reason : esp_err_to_name(err));
}

// True when the exchange itself worked, whatever the sensor answered. Transport failures are
// counted, and FP_ACS_OFFLINE_ERRORS of them in a row take the sensor offline: the task stops
// scanning and goes back to handshaking.
static bool fp_acs_io_ok(esp_err_t err, const char *what)
{
    if (err == ESP_OK) {
        fp_acs_transport_errors = 0;
        return true;
    }
    fp_acs_transport_errors++;
    ESP_LOGW(TAG, "%s: %s", what, fp_acs_io_error_str(err));
    if (fp_acs_transport_errors >= FP_ACS_OFFLINE_ERRORS) {
        fp_acs_set_online(false);
    }
    return false;
}

static void fp_acs_led(uint8_t control, uint8_t colour)
{
    uint8_t code = R503_CODE_OK;
    esp_err_t err = r503_aura_led(control, FP_ACS_LED_SPEED, colour, FP_ACS_LED_FOREVER, &code);
    if (fp_acs_io_ok(err, "AuraLedConfig") && (code != R503_CODE_OK)) {
        ESP_LOGW(TAG, "AuraLedConfig: %s", r503_code_str(code));
    }
}

// Solid green: ready. Solid red: the user store is unavailable, so nobody can be recognised.
static void fp_acs_led_idle(void)
{
    fp_acs_led(R503_LED_ON, fp_acs_store_ok ? R503_LED_GREEN : R503_LED_RED);
}

static bool fp_acs_connect(void)
{
    uint8_t code = R503_CODE_OK;
    esp_err_t err = r503_verify_password(R503_DEFAULT_PASSWORD, &code);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "VfyPwd: %s", esp_err_to_name(err));
        return false;
    }
    if (code != R503_CODE_OK) {
        ESP_LOGE(TAG, "VfyPwd: %s", r503_code_str(code));
        return false;
    }

    uint16_t capacity = 0;
    err = r503_read_capacity(&capacity, &code);
    if ((err != ESP_OK) || (code != R503_CODE_OK) || (capacity == 0)) {
        ESP_LOGE(TAG, "ReadSysPara: %s / %s, capacity %u", esp_err_to_name(err), r503_code_str(code),
                 (unsigned)capacity);
        return false;
    }

    fp_acs_transport_errors = 0;
    fp_acs_lock();
    fp_acs_capacity = capacity;
    fp_acs_online = true;
    fp_acs_unlock();
    ESP_LOGI(TAG, "Sensor online: %u template slots", (unsigned)capacity);

    fp_acs_led_idle();
    return true;
}

// Copies chat, cut to FP_ACS_CHAT_MAX bytes. The cut never splits a UTF-8 character: one that
// would not fit whole is left out whole.
static void fp_acs_copy_chat(char out[FP_ACS_CHAT_MAX + 1], const char *chat)
{
    size_t len = strnlen(chat, FP_ACS_CHAT_MAX + 1);
    if (len > FP_ACS_CHAT_MAX) {
        len = FP_ACS_CHAT_MAX;
        // chat[len] is the first byte left out: while it continues a character, that character
        // started inside the copy, and the copy ends before its lead byte instead.
        while ((len > 0) && ((((uint8_t)chat[len]) & FP_ACS_UTF8_CONT_MASK) == FP_ACS_UTF8_CONT_BITS)) {
            len--;
        }
    }
    memcpy(out, chat, len);
    out[len] = '\0';
}

// chat is the Telegram chat's label, NULL for any other source. Returns the seq the event got.
static uint32_t fp_acs_record_event(fp_acs_source_t source, fp_acs_result_t result, const fp_store_user_t *user,
                                    bool has_score, uint16_t score, const char *chat)
{
    fp_acs_event_t event;
    memset(&event, 0, sizeof(event));
    event.uptime_s = (uint32_t)(esp_timer_get_time() / FP_ACS_US_PER_S);
    event.source = source;
    event.result = result;
    event.user_id = (user != NULL) ? user->id : FP_STORE_NO_USER;
    if (user != NULL) {
        snprintf(event.user_name, sizeof(event.user_name), "%s", user->name);
    }
    event.has_score = has_score;
    event.score = score;
    if (chat != NULL) {
        fp_acs_copy_chat(event.chat, chat);
    }

    // The access log numbers the event. Its seq keeps growing across reboots, so a web page that
    // outlived a reboot cannot take an event of this boot for one of the last (and show that one's
    // image beside it). Logged before the event is published, outside fp_acs's mutex: a reader that
    // sees the new seq finds the record in the log as well.
    uint32_t log_seq = fp_log_append(&event);

    // The sensor task, the bot and the MQTT task all get here, and the seq is handed out under the log's
    // mutex, not this one: an event that lost the race to publish must not replace a newer one.
    fp_acs_lock();
    event.seq = (log_seq != 0) ? log_seq : (fp_acs_last_event.seq + 1);
    if (event.seq > fp_acs_last_event.seq) {
        fp_acs_last_event = event;
    }
    fp_acs_unlock();

    ESP_LOGI(TAG, "Event %" PRIu32 ": %s via %s, user %u \"%s\", score %u", event.seq, fp_acs_result_str(result),
             fp_acs_source_str(source), (unsigned)event.user_id, event.user_name, (unsigned)score);
    return event.seq;
}

// Uploads ImageBuffer as the new last image. The raw 4-bit image lands in the upper half of the
// scratch buffer and is expanded in place into all of it: raw byte k becomes bytes 2k and 2k+1,
// which never reach a raw byte not yet read (those sit at FP_ACS_IMAGE_SIZE / 2 + j, j > k).
// Only a complete image replaces the published one. event_seq is the event it was taken for,
// FP_ACS_IMAGE_NO_EVENT for an enrollment take.
static void fp_acs_upload_image(uint32_t event_seq)
{
    if (fp_acs_image_scratch == NULL) {
        return;
    }

    uint8_t *raw = fp_acs_image_scratch + (FP_ACS_IMAGE_SIZE - R503_IMAGE_RAW_SIZE);
    size_t raw_len = 0;
    uint8_t code = R503_CODE_OK;
    esp_err_t err = r503_up_image(raw, R503_IMAGE_RAW_SIZE, &raw_len, &code);
    if (!fp_acs_io_ok(err, "UpImage")) {
        return;
    }
    if ((code != R503_CODE_OK) || (raw_len != R503_IMAGE_RAW_SIZE)) {
        ESP_LOGW(TAG, "UpImage: %s, %u of %u bytes", r503_code_str(code), (unsigned)raw_len,
                 (unsigned)R503_IMAGE_RAW_SIZE);
        return;
    }

    for (size_t k = 0; k < R503_IMAGE_RAW_SIZE; k++) {
        uint8_t pair = raw[k];
        fp_acs_image_scratch[2 * k] = (uint8_t)((pair >> FP_ACS_NIBBLE_BITS) * FP_ACS_NIBBLE_SCALE);
        fp_acs_image_scratch[(2 * k) + 1] = (uint8_t)((pair & FP_ACS_NIBBLE_MASK) * FP_ACS_NIBBLE_SCALE);
    }

    fp_acs_lock();
    uint8_t *published = fp_acs_image;
    fp_acs_image = fp_acs_image_scratch;
    fp_acs_image_seq++;
    fp_acs_image_event_seq = event_seq;
    fp_acs_unlock();
    fp_acs_image_scratch = published;
}


// ── Scanning ─────────────────────────────────────────────────────────────────────────────

// Img2Tz and Search on the image GenImg has just taken. False when it could not be searched: a
// transport error, an unreadable image, a Search failure. True otherwise, with *code either
// R503_CODE_OK (a match in *page, with *score) or R503_CODE_NOT_FOUND.
static bool fp_acs_identify(uint16_t *page, uint16_t *score, uint8_t *code)
{
    if (!fp_acs_io_ok(r503_img2tz(FP_ACS_SCAN_BUFFER, code), "Img2Tz")) {
        return false;
    }
    if (*code != R503_CODE_OK) {
        ESP_LOGD(TAG, "Img2Tz: %s", r503_code_str(*code));
        return false;
    }

    fp_acs_lock();
    uint16_t capacity = fp_acs_capacity;
    fp_acs_unlock();

    if (!fp_acs_io_ok(r503_search(FP_ACS_SCAN_BUFFER, FP_ACS_SEARCH_START, capacity, page, score, code), "Search")) {
        return false;
    }
    if ((*code != R503_CODE_OK) && (*code != R503_CODE_NOT_FOUND)) {
        ESP_LOGW(TAG, "Search: %s", r503_code_str(*code));
        return false;
    }
    return true;
}

// One GenImg poll of the idle loop.
static void fp_acs_scan(void)
{
    uint8_t code = R503_CODE_OK;
    if (!fp_acs_io_ok(r503_gen_img(&code), "GenImg")) {
        return;
    }

    // After a decision nothing is scanned again until the finger has left the sensor.
    if (fp_acs_awaiting_lift) {
        if (code == R503_CODE_NO_FINGER) {
            fp_acs_awaiting_lift = false;
            fp_acs_led_idle();
        }
        return;
    }
    // No finger, or a capture that failed: the next poll takes another image.
    if (code != R503_CODE_OK) {
        return;
    }

    uint16_t page = 0;
    uint16_t score = 0;
    if (!fp_acs_identify(&page, &score, &code)) {
        // While the finger stays, the next poll captures a fresh image.
        return;
    }

    // No match: fresh images of the same finger are searched while it stays on the sensor, as
    // FingerprintDoorbell does. A lifted finger or a pass that fails ends the attempt as unknown.
    for (unsigned pass = 2; (code == R503_CODE_NOT_FOUND) && (pass <= FP_ACS_SCAN_PASSES); pass++) {
        ESP_LOGD(TAG, "No match, pass %u of %d", pass, FP_ACS_SCAN_PASSES);
        uint8_t image_code = R503_CODE_OK;
        if ((!fp_acs_io_ok(r503_gen_img(&image_code), "GenImg")) || (image_code != R503_CODE_OK) ||
            (!fp_acs_identify(&page, &score, &code)))
        {
            code = R503_CODE_NOT_FOUND;
            break;
        }
    }

    fp_store_user_t user;
    const fp_store_user_t *who = NULL;
    fp_acs_result_t result = FP_ACS_RESULT_UNKNOWN;
    // A matched template no user owns is one a deletion could not remove from the sensor: it
    // belongs to nobody, and is reported as such.
    if ((code == R503_CODE_OK) && fp_store_find_by_slot(page, &user)) {
        who = &user;
        result = (user.enabled && user.allow_fp) ? FP_ACS_RESULT_GRANTED : FP_ACS_RESULT_DENIED;
    }

    if (result == FP_ACS_RESULT_GRANTED) {
        (void)fp_lock_open();
        fp_acs_led(R503_LED_FLASHING, R503_LED_GREEN);
    } else {
        fp_acs_led(R503_LED_FLASHING, R503_LED_RED);
    }

    // After Img2Tz, ImageBuffer holds the thinned ridge map, not the captured image (seen on the
    // device). Uploading before Img2Tz would hold the door for the upload, so the image shown is a
    // fresh capture of the finger still on the sensor, taken once the decision is made (on a grant,
    // with the door already opening) and before the event is recorded, whose log write can take a
    // while. A finger already lifted gives no image: the event then has none, and the web UI does not
    // show an older image beside it.
    uint8_t image_code = R503_CODE_OK;
    bool captured = (fp_acs_io_ok(r503_gen_img(&image_code), "GenImg") && (image_code == R503_CODE_OK));
    uint32_t event_seq = fp_acs_record_event(FP_ACS_SOURCE_FINGERPRINT, result, who, (who != NULL), score, NULL);
    if (captured) {
        fp_acs_upload_image(event_seq);
    }
    fp_acs_awaiting_lift = true;
}


// ── Enrollment ───────────────────────────────────────────────────────────────────────────

static bool fp_acs_cancel_pending(void)
{
    fp_acs_lock();
    bool cancel = fp_acs_cancel_requested;
    fp_acs_unlock();
    return cancel;
}

static void fp_acs_enroll_step(fp_acs_enroll_state_t state, uint8_t step)
{
    fp_acs_lock();
    fp_acs_enroll.state = state;
    fp_acs_enroll.step = step;
    fp_acs_unlock();
}

static void fp_acs_enroll_end(fp_acs_enroll_state_t state, const char *error)
{
    fp_acs_lock();
    fp_acs_enroll.state = state;
    snprintf(fp_acs_enroll.error, sizeof(fp_acs_enroll.error), "%s", (error != NULL) ? error : "");
    fp_acs_cancel_requested = false;
    bool online = fp_acs_online;
    fp_acs_unlock();

    // A finger may still be on the sensor (a cancel, a failed take): the idle loop waits for it to
    // leave rather than scanning it as someone at the door.
    fp_acs_awaiting_lift = true;

    if (state == FP_ACS_ENROLL_STATE_ERROR) {
        ESP_LOGW(TAG, "Enrollment failed: %s", error);
        if (online) {
            fp_acs_led(R503_LED_ON, R503_LED_RED);
            vTaskDelay(pdMS_TO_TICKS(FP_ACS_ERROR_LED_MS));
        }
    } else {
        ESP_LOGI(TAG, "Enrollment %s", fp_acs_enroll_state_str(state));
    }
    if (fp_acs_sensor_online()) {
        fp_acs_led_idle();
    }
}

static void fp_acs_enroll_stopped(fp_acs_wait_t wait)
{
    if (wait == FP_ACS_WAIT_CANCELLED) {
        fp_acs_enroll_end(FP_ACS_ENROLL_STATE_CANCELLED, NULL);
    } else if (wait == FP_ACS_WAIT_TIMEOUT) {
        fp_acs_enroll_end(FP_ACS_ENROLL_STATE_ERROR, FP_ACS_ENROLL_ERROR_TIMEOUT);
    } else {
        fp_acs_enroll_end(FP_ACS_ENROLL_STATE_ERROR, fp_acs_req_str(FP_ACS_REQ_OFFLINE));
    }
}

// Polls GenImg until a finger is on the sensor (want_finger) or off it. Scanning is suspended
// meanwhile, so a finger that does not come within FP_ACS_ENROLL_PLACE_TIMEOUT_MS ends the wait:
// an abandoned enrollment must not keep the door shut.
static fp_acs_wait_t fp_acs_enroll_wait(bool want_finger)
{
    TickType_t started = xTaskGetTickCount();
    for (;;) {
        if (fp_acs_cancel_pending()) {
            return FP_ACS_WAIT_CANCELLED;
        }
        uint8_t code = R503_CODE_OK;
        if (fp_acs_io_ok(r503_gen_img(&code), "GenImg")) {
            if (want_finger ? (code == R503_CODE_OK) : (code == R503_CODE_NO_FINGER)) {
                return FP_ACS_WAIT_DONE;
            }
        } else if (!fp_acs_sensor_online()) {
            return FP_ACS_WAIT_OFFLINE;
        }
        if (want_finger && ((xTaskGetTickCount() - started) >= pdMS_TO_TICKS(FP_ACS_ENROLL_PLACE_TIMEOUT_MS))) {
            return FP_ACS_WAIT_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(FP_ACS_POLL_MS));
    }
}

// Fails the enrollment on a transport error or a refusal. True when the command succeeded. A
// transport error is reported by the driver's own reason (which packet check failed), so that
// GET /fp/status says what went wrong without a UART console on the device. code is a pointer so
// that it is read after the command that fills it: the order in which a call's arguments are
// evaluated is unspecified.
static bool fp_acs_enroll_check(esp_err_t err, const uint8_t *code, const char *what)
{
    char error[FP_ACS_ENROLL_ERROR_MAX];
    if (!fp_acs_io_ok(err, what)) {
        snprintf(error, sizeof(error), "%s: %s", what, fp_acs_io_error_str(err));
    } else if (*code != R503_CODE_OK) {
        snprintf(error, sizeof(error), "%s: %s", what, r503_code_str(*code));
    } else {
        return true;
    }
    fp_acs_enroll_end(FP_ACS_ENROLL_STATE_ERROR, error);
    return false;
}

// Five takes, each converted into its own feature buffer, combined by RegModel into one template
// and stored in the lowest slot no user references.
static void fp_acs_run_enroll(uint8_t user_id)
{
    if (fp_acs_cancel_pending()) {
        fp_acs_enroll_end(FP_ACS_ENROLL_STATE_CANCELLED, NULL);
        return;
    }
    if (!fp_acs_sensor_online()) {
        fp_acs_enroll_end(FP_ACS_ENROLL_STATE_ERROR, fp_acs_req_str(FP_ACS_REQ_OFFLINE));
        return;
    }
    if (!fp_store_get_user(user_id, NULL)) {
        fp_acs_enroll_end(FP_ACS_ENROLL_STATE_ERROR, fp_acs_req_str(FP_ACS_REQ_UNKNOWN_USER));
        return;
    }

    ESP_LOGI(TAG, "Enrolling a finger of user %u", (unsigned)user_id);
    fp_acs_awaiting_lift = false;
    fp_acs_led(R503_LED_FLASHING, R503_LED_PURPLE);

    uint8_t code = R503_CODE_OK;
    for (uint8_t take = 1; take <= FP_ACS_ENROLL_STEPS; take++) {
        fp_acs_enroll_step(FP_ACS_ENROLL_STATE_PLACE, take);
        fp_acs_wait_t wait = fp_acs_enroll_wait(true);
        if (wait != FP_ACS_WAIT_DONE) {
            fp_acs_enroll_stopped(wait);
            return;
        }

        fp_acs_enroll_step(FP_ACS_ENROLL_STATE_PROCESSING, take);
        // The last take is uploaded before Img2Tz, while ImageBuffer certainly still holds it.
        if (take == FP_ACS_ENROLL_STEPS) {
            fp_acs_upload_image(FP_ACS_IMAGE_NO_EVENT);
        }
        if (!fp_acs_enroll_check(r503_img2tz(take, &code), &code, "Img2Tz")) {
            return;
        }

        fp_acs_enroll_step(FP_ACS_ENROLL_STATE_LIFT, take);
        wait = fp_acs_enroll_wait(false);
        if (wait != FP_ACS_WAIT_DONE) {
            fp_acs_enroll_stopped(wait);
            return;
        }
    }

    fp_acs_enroll_step(FP_ACS_ENROLL_STATE_PROCESSING, FP_ACS_ENROLL_STEPS);
    if (!fp_acs_enroll_check(r503_reg_model(&code), &code, "RegModel")) {
        return;
    }

    fp_acs_lock();
    uint16_t capacity = fp_acs_capacity;
    fp_acs_unlock();
    uint16_t slot = 0;
    if (!fp_store_alloc_slot(capacity, &slot)) {
        fp_acs_enroll_end(FP_ACS_ENROLL_STATE_ERROR, "no free slot on the sensor");
        return;
    }
    if (!fp_acs_enroll_check(r503_store(FP_ACS_TEMPLATE_BUFFER, slot, &code), &code, "Store")) {
        return;
    }

    esp_err_t err = fp_store_add_finger(user_id, slot);
    if (err != ESP_OK) {
        // The user was deleted or filled up meanwhile: the template belongs to nobody, so it
        // does not stay on the sensor either.
        (void)fp_acs_io_ok(r503_delete_char(slot, 1, &code), "DeletChar");
        char error[FP_ACS_ENROLL_ERROR_MAX];
        snprintf(error, sizeof(error), "saving the finger: %s", esp_err_to_name(err));
        fp_acs_enroll_end(FP_ACS_ENROLL_STATE_ERROR, error);
        return;
    }

    fp_acs_lock();
    fp_acs_enroll.has_slot = true;
    fp_acs_enroll.slot = slot;
    fp_acs_unlock();
    ESP_LOGI(TAG, "User %u: finger stored in slot %u", (unsigned)user_id, (unsigned)slot);
    fp_acs_enroll_end(FP_ACS_ENROLL_STATE_DONE, NULL);
}


// ── Deletion ─────────────────────────────────────────────────────────────────────────────

// Best effort: a template left behind matches nobody (its slot is no longer referenced) and is
// overwritten when the slot is allocated again.
static void fp_acs_delete_templates(const uint16_t *slots, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!fp_acs_sensor_online()) {
            ESP_LOGW(TAG, "Sensor offline: template %u stays on it until the slot is reused", (unsigned)slots[i]);
            continue;
        }
        uint8_t code = R503_CODE_OK;
        if (fp_acs_io_ok(r503_delete_char(slots[i], 1, &code), "DeletChar") && (code != R503_CODE_OK)) {
            ESP_LOGW(TAG, "DeletChar %u: %s", (unsigned)slots[i], r503_code_str(code));
        }
    }
}

static void fp_acs_handle_command(const fp_acs_cmd_t *cmd)
{
    switch (cmd->type) {
    case FP_ACS_CMD_ENROLL:
        fp_acs_run_enroll(cmd->user_id);
        break;

    case FP_ACS_CMD_DELETE_USER: {
        fp_store_user_t user;
        if (!fp_store_get_user(cmd->user_id, &user)) {
            break;
        }
        fp_acs_delete_templates(user.fingers, user.finger_count);
        esp_err_t err = fp_store_delete_user(cmd->user_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Deleting user %u: %s", (unsigned)cmd->user_id, esp_err_to_name(err));
        }
        break;
    }

    case FP_ACS_CMD_DELETE_FINGER: {
        fp_acs_delete_templates(&cmd->slot, 1);
        esp_err_t err = fp_store_remove_finger(cmd->slot);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Deleting finger %u: %s", (unsigned)cmd->slot, esp_err_to_name(err));
        }
        break;
    }

    default:
        break;
    }
}


// ── The task ─────────────────────────────────────────────────────────────────────────────

static void fp_acs_task(void *arg)
{
    (void)arg;

    for (unsigned attempt = 0; attempt < FP_ACS_BOOT_ATTEMPTS; attempt++) {
        if (fp_acs_connect()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(FP_ACS_BOOT_RETRY_MS));
    }
    if (!fp_acs_sensor_online()) {
        ESP_LOGE(TAG, "No answer from the sensor on UART2; retrying every %d ms", FP_ACS_RECONNECT_MS);
    }

    // Queued requests go first, one per turn, and the sensor is polled only when none is waiting:
    // a deletion is never held up by more than the scan in progress.
    for (;;) {
        bool online = fp_acs_sensor_online();
        fp_acs_cmd_t cmd;
        TickType_t wait = pdMS_TO_TICKS(online ? FP_ACS_POLL_MS : FP_ACS_RECONNECT_MS);
        if (xQueueReceive(fp_acs_queue, &cmd, wait) == pdTRUE) {
            fp_acs_handle_command(&cmd);
            continue;
        }
        if (!online) {
            fp_acs_awaiting_lift = false;
            (void)fp_acs_connect();
            continue;
        }
        fp_acs_scan();
    }
}

void fp_acs_start(void)
{
    fp_acs_mutex = xSemaphoreCreateMutex();
    fp_acs_queue = xQueueCreate(FP_ACS_QUEUE_LENGTH, sizeof(fp_acs_cmd_t));
    if ((fp_acs_mutex == NULL) || (fp_acs_queue == NULL)) {
        ESP_LOGE(TAG, "No memory for the mutex and the queue: fingerprint access control disabled");
        return;
    }

    fp_acs_store_ok = (fp_store_init() == ESP_OK);
    // After the store, which mounts the partition the log lives in.
    fp_log_init();

    fp_acs_image = heap_caps_malloc(FP_ACS_IMAGE_SIZE, MALLOC_CAP_SPIRAM);
    fp_acs_image_scratch = heap_caps_malloc(FP_ACS_IMAGE_SIZE, MALLOC_CAP_SPIRAM);
    if ((fp_acs_image == NULL) || (fp_acs_image_scratch == NULL)) {
        ESP_LOGW(TAG, "No PSRAM for the image buffers: GET /fp/image stays empty");
        heap_caps_free(fp_acs_image);
        heap_caps_free(fp_acs_image_scratch);
        fp_acs_image = NULL;
        fp_acs_image_scratch = NULL;
    }

    if (r503_init() != ESP_OK) {
        ESP_LOGE(TAG, "Sensor UART unavailable: the sensor stays offline");
        return;
    }

    if (xTaskCreate(fp_acs_task, "fp_acs", FP_ACS_TASK_STACK_SIZE, NULL, FP_ACS_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the sensor task: the sensor stays offline");
    }
}


// ── Called from other tasks ──────────────────────────────────────────────────────────────

bool fp_acs_sensor_online(void)
{
    if (fp_acs_mutex == NULL) {
        return false;
    }
    fp_acs_lock();
    bool online = fp_acs_online;
    fp_acs_unlock();
    return online;
}

void fp_acs_get_status(fp_acs_status_t *out)
{
    memset(out, 0, sizeof(*out));
    if (fp_acs_mutex != NULL) {
        fp_acs_lock();
        out->online = fp_acs_online;
        out->capacity = fp_acs_capacity;
        out->enroll = fp_acs_enroll;
        out->image_seq = fp_acs_image_seq;
        out->image_event_seq = fp_acs_image_event_seq;
        fp_acs_unlock();
    }
    out->used = (uint16_t)fp_store_used_slots();
    out->users_rev = fp_store_users_rev();
}

bool fp_acs_get_last_event(fp_acs_event_t *out)
{
    if (fp_acs_mutex == NULL) {
        return false;
    }
    fp_acs_lock();
    *out = fp_acs_last_event;
    fp_acs_unlock();
    return (out->seq != 0);
}

void fp_acs_report_telegram_event(fp_acs_result_t result, uint8_t user_id, const char *chat)
{
    if (fp_acs_mutex == NULL) {
        return;
    }
    fp_store_user_t user;
    bool known = ((user_id != FP_STORE_NO_USER) && fp_store_get_user(user_id, &user));
    fp_acs_record_event(FP_ACS_SOURCE_TELEGRAM, result, known ? &user : NULL, false, 0, chat);
}

void fp_acs_report_mqtt_event(void)
{
    if (fp_acs_mutex == NULL) {
        return;
    }
    fp_acs_record_event(FP_ACS_SOURCE_MQTT, FP_ACS_RESULT_GRANTED, NULL, false, 0, NULL);
}

fp_acs_req_t fp_acs_enroll_start(uint8_t user_id)
{
    if (fp_acs_mutex == NULL) {
        return FP_ACS_REQ_OFFLINE;
    }

    fp_store_user_t user;
    if (!fp_store_get_user(user_id, &user)) {
        return FP_ACS_REQ_UNKNOWN_USER;
    }
    if (user.finger_count >= FP_STORE_FINGERS_MAX) {
        return FP_ACS_REQ_FULL;
    }

    fp_acs_lock();
    bool online = fp_acs_online;
    uint16_t capacity = fp_acs_capacity;
    fp_acs_unlock();
    if (!online) {
        return FP_ACS_REQ_OFFLINE;
    }
    uint16_t slot = 0;
    if (!fp_store_alloc_slot(capacity, &slot)) {
        return FP_ACS_REQ_FULL;
    }

    // The status is reset here rather than by the task: a client polling right after this call
    // must not read the previous enrollment's "done" as this one's.
    fp_acs_req_t req = FP_ACS_REQ_OK;
    fp_acs_cmd_t cmd = {
        .type = FP_ACS_CMD_ENROLL,
        .user_id = user_id,
    };
    fp_acs_lock();
    if (fp_acs_enroll_active_locked()) {
        req = FP_ACS_REQ_BUSY;
    } else if (xQueueSend(fp_acs_queue, &cmd, 0) != pdTRUE) {
        req = FP_ACS_REQ_BUSY;
    } else {
        memset(&fp_acs_enroll, 0, sizeof(fp_acs_enroll));
        fp_acs_enroll.state = FP_ACS_ENROLL_STATE_PLACE;
        fp_acs_enroll.user_id = user_id;
        fp_acs_enroll.step = 1;
        fp_acs_cancel_requested = false;
    }
    fp_acs_unlock();
    return req;
}

void fp_acs_enroll_cancel(void)
{
    if (fp_acs_mutex == NULL) {
        return;
    }
    fp_acs_lock();
    if (fp_acs_enroll_active_locked()) {
        fp_acs_cancel_requested = true;
    }
    fp_acs_unlock();
}

static fp_acs_req_t fp_acs_queue_request(const fp_acs_cmd_t *cmd)
{
    if (fp_acs_queue == NULL) {
        return FP_ACS_REQ_OFFLINE;
    }
    return (xQueueSend(fp_acs_queue, cmd, 0) == pdTRUE) ? FP_ACS_REQ_OK : FP_ACS_REQ_BUSY;
}

fp_acs_req_t fp_acs_delete_user(uint8_t user_id)
{
    if (!fp_store_get_user(user_id, NULL)) {
        return FP_ACS_REQ_UNKNOWN_USER;
    }
    fp_acs_cmd_t cmd = {
        .type = FP_ACS_CMD_DELETE_USER,
        .user_id = user_id,
    };
    return fp_acs_queue_request(&cmd);
}

fp_acs_req_t fp_acs_delete_finger(uint16_t slot)
{
    if (!fp_store_find_by_slot(slot, NULL)) {
        return FP_ACS_REQ_UNKNOWN_SLOT;
    }
    fp_acs_cmd_t cmd = {
        .type = FP_ACS_CMD_DELETE_FINGER,
        .slot = slot,
    };
    return fp_acs_queue_request(&cmd);
}

size_t fp_acs_copy_image(uint8_t *out, size_t out_size, uint32_t *seq)
{
    if ((fp_acs_mutex == NULL) || (out == NULL) || (out_size < FP_ACS_IMAGE_SIZE)) {
        return 0;
    }
    size_t copied = 0;
    fp_acs_lock();
    if ((fp_acs_image_seq != 0) && (fp_acs_image != NULL)) {
        memcpy(out, fp_acs_image, FP_ACS_IMAGE_SIZE);
        copied = FP_ACS_IMAGE_SIZE;
        if (seq != NULL) {
            *seq = fp_acs_image_seq;
        }
    }
    fp_acs_unlock();
    return copied;
}

const char *fp_acs_source_str(fp_acs_source_t source)
{
    switch (source) {
    case FP_ACS_SOURCE_TELEGRAM: return "telegram";
    case FP_ACS_SOURCE_MQTT:     return "mqtt";
    default:                     return "fingerprint";
    }
}

const char *fp_acs_result_str(fp_acs_result_t result)
{
    switch (result) {
    case FP_ACS_RESULT_GRANTED: return "granted";
    case FP_ACS_RESULT_DENIED:  return "denied";
    default:                    return "unknown";
    }
}

const char *fp_acs_enroll_state_str(fp_acs_enroll_state_t state)
{
    switch (state) {
    case FP_ACS_ENROLL_STATE_PLACE:      return "place";
    case FP_ACS_ENROLL_STATE_LIFT:       return "lift";
    case FP_ACS_ENROLL_STATE_PROCESSING: return "processing";
    case FP_ACS_ENROLL_STATE_DONE:       return "done";
    case FP_ACS_ENROLL_STATE_ERROR:      return "error";
    case FP_ACS_ENROLL_STATE_CANCELLED:  return "cancelled";
    default:                             return "idle";
    }
}

const char *fp_acs_req_str(fp_acs_req_t req)
{
    switch (req) {
    case FP_ACS_REQ_OK:           return "ok";
    case FP_ACS_REQ_UNKNOWN_USER: return "unknown user";
    case FP_ACS_REQ_UNKNOWN_SLOT: return "unknown finger slot";
    case FP_ACS_REQ_OFFLINE:      return "sensor offline";
    case FP_ACS_REQ_BUSY:         return "busy";
    case FP_ACS_REQ_FULL:         return "no room for another finger";
    default:                      return "error";
    }
}

#endif // FP_ACS_ENABLED
