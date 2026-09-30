#pragma once

/*
 * Fingerprint access control for WB-MGU: a Grow R503 sensor in the WBE2 slot (serial port 2,
 * plain 3.3 V UART) decides who may open the door, and one channel of a Wiren Board relay
 * module on RS-485 port 1, driven by the Modbus master, opens it.
 *
 * The feature needs that board (the sensor sits in its WBE2 slot), the master role (the relay
 * is a Modbus slave on port 1) and real hardware, so it is compiled in and started only in that
 * one combination; everywhere else its sources compile to nothing and this header carries only
 * FP_ACS_ENABLED.
 *
 *   r503.c        the sensor's packet protocol
 *   fp_store.c    users, their fingers and the allowed Telegram chats, in the "fp_nvs" partition
 *   fp_lock.c     the lock pulse
 *   fp_log.c      the log of opening attempts, kept in "fp_nvs" too, and the SNTP clock it is stamped with
 *   fp_acs.c      the one task that owns the sensor: scanning, enrollment, deletion, last image
 *   fp_console.c  a RAM copy of the device log, for GET /fp/console
 *   fp_http.c     GET/POST /fp/...
 */

// Explicit: CONFIG_WB_MODBUS_ROLE_MASTER comes from sdkconfig.h, and mb_role.h silently resolves
// to the slave role without it, which would compile the feature out on the one build it is for.
#include "sdkconfig.h"
#include "mb_role.h"

#if (WB_MB_ROLE_MASTER && (!QEMU_BUILD) && defined(MODEL_mgu_v1))
    #define FP_ACS_ENABLED      1
#else
    #define FP_ACS_ENABLED      0
#endif

#if FP_ACS_ENABLED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>
#include <esp_http_server.h>

#include "fp_store.h"
#include "r503.h"

// The last image, as GET /fp/image serves it: 8-bit grey, row by row.
#define FP_ACS_IMAGE_WIDTH          R503_IMAGE_WIDTH
#define FP_ACS_IMAGE_HEIGHT         R503_IMAGE_HEIGHT
#define FP_ACS_IMAGE_SIZE           (FP_ACS_IMAGE_WIDTH * FP_ACS_IMAGE_HEIGHT)
#define FP_ACS_IMAGE_NO_EVENT       0           // image_event_seq of an enrollment take: event seqs start at 1

#define FP_ACS_ENROLL_STEPS         5       // finger placements per enrollment
#define FP_ACS_ENROLL_ERROR_MAX     64
#define FP_ACS_CHAT_MAX             31      // bytes of UTF-8 of an event's chat, cut at a character boundary

typedef enum {
    FP_ACS_SOURCE_FINGERPRINT,
    FP_ACS_SOURCE_TELEGRAM,
} fp_acs_source_t;

typedef enum {
    FP_ACS_RESULT_GRANTED,
    FP_ACS_RESULT_DENIED,       // a known user who may not open this way (disabled, or not allowed)
    FP_ACS_RESULT_UNKNOWN,      // nobody this device knows
} fp_acs_result_t;

typedef struct {
    uint32_t        seq;                                // the access log's seq; per boot without the log
    uint32_t        uptime_s;
    fp_acs_source_t source;
    fp_acs_result_t result;
    uint8_t         user_id;                            // FP_STORE_NO_USER when nobody
    char            user_name[FP_STORE_NAME_MAX + 1];   // empty when nobody
    bool            has_score;                          // a fingerprint match of a known user
    uint16_t        score;
    char            chat[FP_ACS_CHAT_MAX + 1];          // the Telegram chat's title, or its id; empty for a fingerprint
} fp_acs_event_t;

typedef enum {
    FP_ACS_ENROLL_STATE_IDLE,
    FP_ACS_ENROLL_STATE_PLACE,          // waiting for the finger
    FP_ACS_ENROLL_STATE_LIFT,           // waiting for the finger to be lifted
    FP_ACS_ENROLL_STATE_PROCESSING,
    FP_ACS_ENROLL_STATE_DONE,
    FP_ACS_ENROLL_STATE_ERROR,
    FP_ACS_ENROLL_STATE_CANCELLED,
} fp_acs_enroll_state_t;

typedef struct {
    fp_acs_enroll_state_t state;
    uint8_t               user_id;                      // FP_STORE_NO_USER when idle
    uint8_t               step;                         // 1..FP_ACS_ENROLL_STEPS, 0 when idle
    bool                  has_slot;                     // set once the template is stored
    uint16_t              slot;
    char                  error[FP_ACS_ENROLL_ERROR_MAX];   // empty unless state is ERROR
} fp_acs_enroll_t;

typedef struct {
    bool            online;
    uint16_t        capacity;           // template slots of the sensor; 0 until it has answered once
    uint16_t        used;               // slots referenced by users
    uint32_t        users_rev;
    fp_acs_enroll_t enroll;
    uint32_t        image_seq;          // 0 while there is no image
    uint32_t        image_event_seq;    // the last_event seq the image was taken for; FP_ACS_IMAGE_NO_EVENT: none
} fp_acs_status_t;

// Why a request to the sensor task was not taken.
typedef enum {
    FP_ACS_REQ_OK,
    FP_ACS_REQ_UNKNOWN_USER,
    FP_ACS_REQ_UNKNOWN_SLOT,
    FP_ACS_REQ_OFFLINE,
    FP_ACS_REQ_BUSY,
    FP_ACS_REQ_FULL,
} fp_acs_req_t;

// Loads the store, installs the sensor UART and starts the task that owns the sensor. Never
// aborts: every failure is logged and leaves the rest of the firmware running. Call once, after
// fp_lock_start(); it needs NVS but not the network.
void fp_acs_start(void);

bool fp_acs_sensor_online(void);
void fp_acs_get_status(fp_acs_status_t *out);
// False while there has been no event since boot.
bool fp_acs_get_last_event(fp_acs_event_t *out);
// Records an opening attempt made through Telegram as the last event. user_id may be
// FP_STORE_NO_USER; the name is looked up in the store. chat names the chat the attempt came from
// (its title, or its id when it has none) and is cut to FP_ACS_CHAT_MAX bytes at a UTF-8 character
// boundary. Opening the lock is the caller's (fp_lock_open()).
void fp_acs_report_telegram_event(fp_acs_result_t result, uint8_t user_id, const char *chat);

// The three requests below are queued to the sensor task and return at once; their outcome
// shows in the status (enrollment) and in users_rev (deletions).
//
// Starts enrolling one more finger of the user: FP_ACS_REQ_UNKNOWN_USER, _OFFLINE, _BUSY (an
// enrollment is running, or the queue is full), _FULL (the user has FP_STORE_FINGERS_MAX
// fingers, or the sensor has no free slot).
fp_acs_req_t fp_acs_enroll_start(uint8_t user_id);
// Cancels the running enrollment, if any.
void         fp_acs_enroll_cancel(void);
// Deletes the templates from the sensor, then the user or finger from the store. When the
// sensor cannot be reached the store is updated anyway - the orphaned template matches nobody
// and its slot is reused - so a user can always be removed. _UNKNOWN_USER / _UNKNOWN_SLOT, _BUSY.
fp_acs_req_t fp_acs_delete_user(uint8_t user_id);
fp_acs_req_t fp_acs_delete_finger(uint16_t slot);

// Copies the last image (FP_ACS_IMAGE_SIZE bytes) and returns its size, or 0 when there is none
// or out_size is too small. *seq (may be NULL) receives its sequence number.
size_t fp_acs_copy_image(uint8_t *out, size_t out_size, uint32_t *seq);

// The wire names used by the HTTP API: "fingerprint", "granted", "place", "sensor offline", ...
const char *fp_acs_source_str(fp_acs_source_t source);
const char *fp_acs_result_str(fp_acs_result_t result);
const char *fp_acs_enroll_state_str(fp_acs_enroll_state_t state);
const char *fp_acs_req_str(fp_acs_req_t req);

// fp_http.c: registers the twelve /fp/ endpoints. Every one of them requires a session.
void fp_http_register_handlers(httpd_handle_t server);

#endif // FP_ACS_ENABLED
