#include "virtual_io_qemu.h"
#include "gpio_expander.h"

#include <string.h>

#include "esp_bit_defs.h"    // BIT<n>, expanded by the SOC_GPIO_VALID_*_MASK macros
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/errno.h"
#include "lwip/sockets.h"
#include "soc/gpio_num.h"
#include "soc/soc_caps.h"   // SOC_GPIO_VALID_OUTPUT_GPIO_MASK (input-only pads)

// Virtual IO state bus for the QEMU build.
//
// Wire protocol (UDP, port 5570): each datagram is exactly 5 ASCII bytes
//   <T><NN>/<X>
//     T  : 'E' = expander pin, 'G' = native ESP32 GPIO level,
//          'D' = native GPIO direction, 'V' = native GPIO direction violation
//     NN : zero-padded 2-digit number (expander 00..15; native = any GPIO num)
//     /  : literal separator, always at index 3
//     X  : for 'E'/'G' the RAW physical pin level ('0'/'1'); for 'D' the
//          direction ('1' OUTPUT, '0' INPUT); for 'V' the violation cause
//          ('0' host wrote OUTPUT, '1' firmware drove an input-only pin,
//          '2' host operated an uninitialized pin)
// A single optional trailing '\n' is tolerated. Parsing is positional.
//
// Native GPIO model: a generic per-GPIO table indexed by GPIO number holds
// {dir_state, level, out_latch}. The direction is NOT hardcoded: it is driven
// solely by the firmware's real ESP-IDF GPIO calls, transparently intercepted by
// the linker --wrap shim (main/qemu/gpio_shim_qemu.c) which forwards them here.
// A pin starts UNCONFIGURED and only becomes INPUT/OUTPUT once the firmware
// configures it. 'D' records are emitted on a direction change and in the full
// dump (only for pins currently INPUT or OUTPUT).
//
// The model mirrors the silicon's two distinct pieces of per-pad state:
//   out_latch : the output data register. gpio_set_level() writes it REGARDLESS
//               of the pad direction — that is what makes the glitch-free
//               "set the level, then switch to OUTPUT" idiom work on real
//               hardware (see serial.c: dir_pin forced LOW without a glitch).
//               A latch write on a pad that is not an output changes nothing on
//               the line and is NOT a violation.
//   level     : what is actually on the line, i.e. what the host observes and
//               what gpio_get_level() returns. An OUTPUT pad drives level from
//               out_latch (the latch is applied on the INPUT/UNCONFIGURED ->
//               OUTPUT transition); an INPUT pad's level comes from the host.
//
// A 'V' record is emitted (NON-fatally) when the model is actually violated:
//   V/0 the host drove a firmware-owned OUTPUT pin           -> write rejected;
//   V/1 the firmware configured an input-only pad as OUTPUT  -> config rejected
//       (ESP32 GPIO34..39 physically cannot drive a line, so firmware driving
//        one is a real bug the model must catch);
//   V/2 the host operated an UNCONFIGURED pin                -> write rejected.
// Expander ('E') pins have no direction model (all behave as outputs).
//
// What this enforcement does NOT catch (known limits, stated so nobody reads more
// safety into the model than it has):
//   - "the firmware grabbed a line that should stay an input" is only caught when
//     the pad is INPUT-ONLY (GPIO34..39 -> V/1 at configuration time). On a normal
//     output-capable pad, configuring it as OUTPUT is legal by construction, and a
//     gpio_set_level() while it is an INPUT is latch-only (see below) — so neither
//     raises a violation. Since the firmware never configures GPIO34..39 as OUTPUT,
//     V/1 is effectively unreachable today: it is a guard, not active coverage.
//   - a gpio_set_level() on a pin that was never configured does not reach the
//     model at all: __wrap_gpio_set_level() gates on vio_native_is_tracked(), so
//     out_latch is NOT updated for an UNCONFIGURED pin the way real silicon would.
//     Harmless here (the firmware configures a pin before driving it), and the gate
//     is what keeps the model's mutex off every gpio_set_level() call site.
//
// QEMU usermode networking (-nic user + hostfwd) only NATs host->guest, so
// unsolicited guest->host UDP does not arrive. The guest therefore LEARNS the
// host's address from the first received datagram (recvfrom source address) and
// only ever sends TX records back to that last-known peer (through the existing
// NAT mapping). When a peer first becomes known (or changes), a full state dump
// is sent so the host gets the initial state.

#define VIRTUAL_IO_UDP_PORT             5570
#define VIRTUAL_IO_RX_TASK_STACK_SIZE   4096
#define VIRTUAL_IO_RX_TASK_PRIORITY     5
#define VIRTUAL_IO_RECORD_LEN           5

// Number of native GPIO slots tracked in the model (0..GPIO_NUM_MAX-1).
#define VIRTUAL_IO_NATIVE_GPIO_COUNT    GPIO_NUM_MAX

static const char *TAG = "virtual_io_qemu";

// Direction encoding for native pins (matches the on-the-wire 'D' record).
#define NATIVE_DIR_INPUT    0
#define NATIVE_DIR_OUTPUT   1

// Violation cause codes (matches the on-the-wire 'V' record).
#define VIOLATION_HOST_WROTE_OUTPUT     0   // host drove an OUTPUT pin
#define VIOLATION_FW_WROTE_INPUT        1   // firmware drove an input-only pad
#define VIOLATION_OPERATE_UNINIT        2   // operate an UNCONFIGURED pin

// Per-GPIO model entry:
//   dir_state - one of VIO_DIR_*, learned from the firmware's gpio calls;
//   level     - what is on the line (host-observable, gpio_get_level());
//   out_latch - the output data register, written by gpio_set_level() in ANY
//               direction and applied to the line when the pad becomes OUTPUT.
typedef struct {
    vio_dir_state_t dir_state;
    int level;
    int out_latch;
} native_pin_t;

static struct {
    SemaphoreHandle_t mutex;
    uint16_t exp_shadow;            // raw 16-bit expander output register; bit index == pin number
    native_pin_t native[VIRTUAL_IO_NATIVE_GPIO_COUNT]; // per-GPIO direction + level model
    int sock;                       // UDP socket fd (-1 = invalid)
    struct sockaddr_in peer;        // last-known host address
    bool peer_known;
} s_ctx = {
    .mutex = NULL,
    .exp_shadow = 0,
    // native[] zero-initialized: dir_state == VIO_DIR_UNCONFIGURED, level == 0.
    // NO hardcoded per-pin defaults: directions are learned from the firmware's
    // real gpio_config/gpio_set_direction/uart_set_pin via the wrap shim.
    .sock = -1,
    .peer_known = false,
};

// Build the 5-byte wire record (no I/O). 'V' carries a multi-valued cause (0..7);
// E/G/D are 0/1 only. Layout matches api_tests/io_bus_helpers.py: type@0, NN@1..2,
// '/'@3 (SEP_INDEX), level@4 (LEVEL_INDEX), total 5 (RECORD_LEN).
static void encode_record(char type, int num, int value, char *out)
{
    out[0] = type;
    out[1] = (char)('0' + ((num / 10) % 10));
    out[2] = (char)('0' + (num % 10));
    out[3] = '/';
    out[4] = (type == 'V') ? (char)('0' + (value & 0x7)) : (value ? '1' : '0');
}

// Build the 5-byte record and send it to the learned peer. Caller MUST hold the
// mutex. Silently drops if no peer is known or the socket is invalid.
static void send_record_locked(char type, int num, int value)
{
    if (!s_ctx.peer_known || (s_ctx.sock < 0)) {
        return;
    }

    char buf[VIRTUAL_IO_RECORD_LEN];
    encode_record(type, num, value, buf);

    sendto(s_ctx.sock, buf, sizeof(buf), 0,
           (struct sockaddr *)&s_ctx.peer, sizeof(s_ctx.peer));
}

// True if gpio_num is a valid index into the native model table.
static bool native_index_valid(int gpio_num)
{
    return (gpio_num >= 0) && (gpio_num < VIRTUAL_IO_NATIVE_GPIO_COUNT);
}

// True if the pad can physically drive a line. On the ESP32 GPIO34..39 are
// input-only, so configuring one as an OUTPUT is a firmware bug the model must
// flag (V/1) rather than emulate. Taken from the SoC caps, so it stays correct
// if the target ever changes.
static bool native_pin_output_capable(int gpio_num)
{
    return (SOC_GPIO_VALID_OUTPUT_GPIO_MASK & (1ULL << gpio_num)) != 0;
}

// Emit a full dump of all tracked signals to the (just-learned) peer.
// Caller MUST hold the mutex.
static void send_full_dump_locked(void)
{
    // Expander pins (unchanged): E00..E15.
    for (int pin = 0; pin < 16; pin++) {
        int level = (s_ctx.exp_shadow >> pin) & 1;
        send_record_locked('E', pin, level);
    }
    // Native pins: emit D + G only for pins currently configured INPUT/OUTPUT,
    // so the dump reflects the real, code-driven state (no defaults).
    for (int pin = 0; pin < VIRTUAL_IO_NATIVE_GPIO_COUNT; pin++) {
        vio_dir_state_t dir = s_ctx.native[pin].dir_state;
        if (dir == VIO_DIR_INPUT) {
            send_record_locked('D', pin, NATIVE_DIR_INPUT);
            send_record_locked('G', pin, s_ctx.native[pin].level);
        } else if (dir == VIO_DIR_OUTPUT) {
            send_record_locked('D', pin, NATIVE_DIR_OUTPUT);
            send_record_locked('G', pin, s_ctx.native[pin].level);
        }
    }
}

// --- gpio_expander.h contract (RAM-backed shadow) ------------------------------

esp_err_t gpio_expander_init(esp_io_expander_handle_t* handle)
{
    if (s_ctx.mutex == NULL) {
        s_ctx.mutex = xSemaphoreCreateMutex();
        if (s_ctx.mutex == NULL) {
            ESP_LOGE(TAG, "Unable to create virtual IO mutex");
            return ESP_FAIL;
        }
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.exp_shadow = 0;
    xSemaphoreGive(s_ctx.mutex);

    if (handle != NULL) {
        *handle = NULL;
    }

    ESP_LOGI(TAG, "Virtual GPIO expander initialized (RAM shadow)");
    return ESP_OK;
}

esp_err_t gpio_expander_set_dir(uint32_t pin_num_mask, esp_io_expander_dir_t direction)
{
    (void)pin_num_mask;
    (void)direction;
    // Direction is a no-op for the virtual expander (all pins act as outputs).
    return ESP_OK;
}

esp_err_t gpio_expander_set_level(uint32_t pin_num_mask, uint8_t level)
{
    if (s_ctx.mutex == NULL) {
        return ESP_FAIL;
    }

    int bit_level = level ? 1 : 0;

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    for (int pin = 0; pin < 16; pin++) {
        if (!(pin_num_mask & (1u << pin))) {
            continue;
        }
        int old_level = (s_ctx.exp_shadow >> pin) & 1;
        if (old_level == bit_level) {
            continue;
        }
        if (bit_level) {
            s_ctx.exp_shadow |= (uint16_t)(1u << pin);
        } else {
            s_ctx.exp_shadow &= (uint16_t)~(1u << pin);
        }
        send_record_locked('E', pin, bit_level);
    }
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

esp_err_t gpio_expander_set_out_dir_and_level(uint32_t pin_num_mask, uint8_t level)
{
    // Direction is a no-op; behaves like set_level.
    return gpio_expander_set_level(pin_num_mask, level);
}

esp_err_t gpio_expander_get_level(uint32_t pin_num_mask, uint32_t *level_mask)
{
    if ((s_ctx.mutex == NULL) || (level_mask == NULL)) {
        return ESP_FAIL;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    // Return masked shadow bits, preserving bit positions (matches the real driver).
    *level_mask = (uint32_t)s_ctx.exp_shadow & pin_num_mask;
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

// --- Native virtual GPIO (driven ONLY by the wrap shim + RX) --------------------

// Apply the output latch to the line on a transition to OUTPUT. Caller MUST hold
// the mutex (pre-init callers hold nothing, but then send_record_locked() is a
// no-op: no peer is known yet). This is the model half of the hardware's
// glitch-free idiom: the pad starts driving whatever the output data register
// already holds, so a gpio_set_level() done BEFORE the direction switch takes
// effect the moment the pad becomes an output. Emits G only on a real change.
static void native_apply_latch_locked(int gpio_num)
{
    int latched = s_ctx.native[gpio_num].out_latch;
    if (s_ctx.native[gpio_num].level == latched) {
        return;
    }
    s_ctx.native[gpio_num].level = latched;
    send_record_locked('G', gpio_num, latched);
}

// Set the model direction for a native GPIO. Called from the wrap shim when the
// firmware configures a pin (gpio_config / gpio_set_direction / gpio_reset_pin /
// uart_set_pin). On a change to INPUT/OUTPUT a 'D' record is emitted; switching
// to UNCONFIGURED/DISABLE emits nothing (there is no 'D' for an unconfigured pin).
// Configuring an input-only pad as OUTPUT is rejected with a V/1 violation.
void vio_native_set_direction(int gpio_num, vio_dir_state_t dir)
{
    if (!native_index_valid(gpio_num)) {
        return;
    }

    if (s_ctx.mutex == NULL) {
        // Pre-init: just update the model, skip the send. Seed idle-HIGH on a
        // fresh transition to INPUT (see the locked branch below for rationale).
        if ((dir == VIO_DIR_OUTPUT) && !native_pin_output_capable(gpio_num)) {
            return; // input-only pad cannot become an output
        }
        if (s_ctx.native[gpio_num].dir_state != dir) {
            s_ctx.native[gpio_num].dir_state = dir;
            if (dir == VIO_DIR_INPUT) {
                s_ctx.native[gpio_num].level = 1;
            } else if (dir == VIO_DIR_OUTPUT) {
                native_apply_latch_locked(gpio_num);
            }
        }
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    if ((dir == VIO_DIR_OUTPUT) && !native_pin_output_capable(gpio_num)) {
        // The firmware asked an input-only pad (ESP32 GPIO34..39) to drive the
        // line. Real silicon simply cannot: the pad has no output driver. Flag it
        // and keep the pin's current direction, so the bug is visible to the host
        // instead of being silently emulated.
        ESP_LOGE(TAG, "Direction violation: firmware configured input-only pin G%02d as OUTPUT (rejected)", gpio_num);
        send_record_locked('V', gpio_num, VIOLATION_FW_WROTE_INPUT);
        xSemaphoreGive(s_ctx.mutex);
        return;
    }
    if (s_ctx.native[gpio_num].dir_state != dir) {
        s_ctx.native[gpio_num].dir_state = dir;
        if (dir == VIO_DIR_INPUT) {
            send_record_locked('D', gpio_num, NATIVE_DIR_INPUT);
            // Idle-HIGH default on a fresh transition to INPUT. ESP32 input-only
            // pins (e.g. GPIO34-39, the config button) have no internal pulls and
            // the board uses an external pull-up, so a floating/unconnected input
            // idles HIGH. Without this the model level would stay 0 (= active-LOW
            // button "pressed") at boot with no host connected, which spuriously
            // triggers the long-press factory reset. We seed level=1 only on the
            // UNCONFIGURED/OUTPUT -> INPUT transition (this branch runs only when
            // dir_state actually changed), so a pin already INPUT and driven by the
            // host is NOT force-reset by a redundant reconfigure. The output latch
            // is deliberately left alone: on real hardware it keeps its value while
            // the pad is an input.
            s_ctx.native[gpio_num].level = 1;
            send_record_locked('G', gpio_num, 1);
        } else if (dir == VIO_DIR_OUTPUT) {
            send_record_locked('D', gpio_num, NATIVE_DIR_OUTPUT);
            // The pad now drives the value already held in the output latch.
            native_apply_latch_locked(gpio_num);
        }
        // UNCONFIGURED/DISABLE: no 'D' record emitted.
    }
    xSemaphoreGive(s_ctx.mutex);
}

// FIRMWARE write path (gpio_set_level via the wrap shim). Rules (matching the
// silicon, where gpio_set_level() writes the output data register no matter what
// the pad direction is):
//   ALWAYS       -> update out_latch.
//   OUTPUT       -> the pad drives the line: update level, emit G on change.
//   INPUT        -> latch-only: the pad does not drive the line, so the level is
//                   unchanged and NO record is emitted. This is legal, NOT a
//                   violation: it is exactly the glitch-free "set level, then
//                   switch to OUTPUT" idiom (serial.c forcing dir_pin LOW).
//   UNCONFIGURED -> latch-only, same as INPUT.
// A firmware bug that actually drives an input line is caught at configuration
// time instead (V/1 on an input-only pad, see vio_native_set_direction).
void vio_native_fw_set_level(int gpio_num, int level)
{
    if (!native_index_valid(gpio_num)) {
        return;
    }
    int bit_level = level ? 1 : 0;

    if (s_ctx.mutex == NULL) {
        // Pre-init: update the latch (and the line if the pad already drives it);
        // no peer to notify yet.
        s_ctx.native[gpio_num].out_latch = bit_level;
        if (s_ctx.native[gpio_num].dir_state == VIO_DIR_OUTPUT) {
            s_ctx.native[gpio_num].level = bit_level;
        }
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.native[gpio_num].out_latch = bit_level;
    if (s_ctx.native[gpio_num].dir_state == VIO_DIR_OUTPUT) {
        if (s_ctx.native[gpio_num].level != bit_level) {
            s_ctx.native[gpio_num].level = bit_level;
            send_record_locked('G', gpio_num, bit_level);
        }
    }
    xSemaphoreGive(s_ctx.mutex);
}

// Returns the current native shadow level for the given GPIO number (for the
// wrap shim's gpio_get_level on a tracked pin).
int vio_native_get_level(int gpio_num)
{
    if (!native_index_valid(gpio_num)) {
        return 0;
    }

    int level = 0;
    if (s_ctx.mutex != NULL) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    }
    level = s_ctx.native[gpio_num].level;
    if (s_ctx.mutex != NULL) {
        xSemaphoreGive(s_ctx.mutex);
    }
    return level;
}

// True if the wrap shim should serve gpio_get_level from the model (pin tracked,
// i.e. configured as INPUT or OUTPUT). Unconfigured pins fall through to __real_.
bool vio_native_is_tracked(int gpio_num)
{
    if (!native_index_valid(gpio_num)) {
        return false;
    }
    bool tracked;
    if (s_ctx.mutex != NULL) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    }
    tracked = (s_ctx.native[gpio_num].dir_state != VIO_DIR_UNCONFIGURED);
    if (s_ctx.mutex != NULL) {
        xSemaphoreGive(s_ctx.mutex);
    }
    return tracked;
}

// --- UDP bus -------------------------------------------------------------------

// HOST/RX write path for a native G record. Rules:
//   INPUT        -> legal: update level, emit echo G on change.
//   OUTPUT       -> violation V/0 (host drove an output): log, emit, REJECT.
//   UNCONFIGURED -> violation V/2 (operate uninitialized): log, emit, REJECT.
// Caller must NOT hold the mutex.
static void virtual_io_native_apply_from_host(int gpio_num, int level)
{
    if (!native_index_valid(gpio_num)) {
        return;
    }
    int bit_level = level ? 1 : 0;

    if (s_ctx.mutex == NULL) {
        // Pre-init: update shadow only (no peer to notify, no enforcement).
        s_ctx.native[gpio_num].level = bit_level;
        return;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    vio_dir_state_t dir = s_ctx.native[gpio_num].dir_state;
    if (dir == VIO_DIR_INPUT) {
        if (s_ctx.native[gpio_num].level != bit_level) {
            s_ctx.native[gpio_num].level = bit_level;
            send_record_locked('G', gpio_num, bit_level);
        }
    } else if (dir == VIO_DIR_OUTPUT) {
        ESP_LOGE(TAG, "Direction violation: host drove OUTPUT pin G%02d (rejected)", gpio_num);
        send_record_locked('V', gpio_num, VIOLATION_HOST_WROTE_OUTPUT);
    } else { // VIO_DIR_UNCONFIGURED
        ESP_LOGE(TAG, "Direction violation: host operated UNCONFIGURED pin G%02d (rejected)", gpio_num);
        send_record_locked('V', gpio_num, VIOLATION_OPERATE_UNINIT);
    }
    xSemaphoreGive(s_ctx.mutex);
}

// Validate one received datagram and decode (type,num,value). Returns true if the
// record is well-formed for RX: optional single trailing '\n', exact 5-byte body,
// type 'E' or 'G' (D/V are TX-only and rejected on RX), 2-digit number, '/' at
// index 3, level strictly '0'/'1'. Mirrors api_tests/io_bus_helpers.py IoBus._parse.
static bool parse_record(const char *buf, int len, char *type_out, int *num_out, int *value_out)
{
    // Tolerate an optional trailing '\n'.
    if ((len == VIRTUAL_IO_RECORD_LEN + 1) && (buf[VIRTUAL_IO_RECORD_LEN] == '\n')) {
        len = VIRTUAL_IO_RECORD_LEN;
    }
    if (len != VIRTUAL_IO_RECORD_LEN) {
        return false;
    }

    char type = buf[0];
    if ((type != 'E') && (type != 'G')) {
        return false;
    }
    if ((buf[1] < '0') || (buf[1] > '9') || (buf[2] < '0') || (buf[2] > '9')) {
        return false;
    }
    if (buf[3] != '/') {
        return false;
    }
    if ((buf[4] != '0') && (buf[4] != '1')) {
        return false;
    }

    *type_out = type;
    *num_out = (buf[1] - '0') * 10 + (buf[2] - '0');
    *value_out = (buf[4] - '0');
    return true;
}

// Parse one received datagram and apply it to the virtual shadow.
// Native 'G' records are routed through the host apply path; 'E' records drive
// the virtual expander. Malformed datagrams are ignored.
static void handle_rx_record(const char *buf, int len)
{
    char type;
    int num, level;
    if (!parse_record(buf, len, &type, &num, &level)) {
        return;
    }

    if (type == 'G') {
        // Host-driven native GPIO write. Goes through the HOST apply path: the
        // host may drive INPUT pins (e.g. G34 config button) but not
        // firmware-owned OUTPUT pins, nor UNCONFIGURED pins — those are flagged
        // as violations and rejected.
        virtual_io_native_apply_from_host(num, level);
    } else { // 'E'
        if (num < 16) {
            gpio_expander_set_level((uint32_t)(1u << num), (uint8_t)level);
        }
    }
}

static void virtual_io_rx_task(void *arg)
{
    (void)arg;
    char rx_buffer[16];

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int len = recvfrom(s_ctx.sock, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&source_addr, &addr_len);
        if (len < 0) {
            ESP_LOGW(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Learn/update the peer address from every received datagram. On a
        // (new) peer, immediately send a full state dump so the host gets the
        // initial state. This is the only way guest->host UDP reaches the host
        // through QEMU usermode NAT.
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        bool peer_changed = !s_ctx.peer_known ||
                            (s_ctx.peer.sin_addr.s_addr != source_addr.sin_addr.s_addr) ||
                            (s_ctx.peer.sin_port != source_addr.sin_port);
        s_ctx.peer = source_addr;
        s_ctx.peer_known = true;
        if (peer_changed) {
            send_full_dump_locked();
        }
        xSemaphoreGive(s_ctx.mutex);

        handle_rx_record(rx_buffer, len);
    }
}

esp_err_t virtual_io_init(void)
{
    if (s_ctx.mutex == NULL) {
        // gpio_expander_init() normally runs first and creates the mutex; create
        // it here too so the bus works even if init order changes.
        s_ctx.mutex = xSemaphoreCreateMutex();
        if (s_ctx.mutex == NULL) {
            ESP_LOGE(TAG, "Unable to create virtual IO mutex");
            return ESP_OK; // non-fatal
        }
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create UDP socket: errno %d", errno);
        return ESP_OK; // non-fatal
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(VIRTUAL_IO_UDP_PORT);

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGE(TAG, "Unable to bind UDP socket on port %d: errno %d", VIRTUAL_IO_UDP_PORT, errno);
        close(sock);
        return ESP_OK; // non-fatal
    }

    s_ctx.sock = sock;

    BaseType_t ret = xTaskCreate(virtual_io_rx_task, "virtual_io_rx",
                                 VIRTUAL_IO_RX_TASK_STACK_SIZE, NULL,
                                 VIRTUAL_IO_RX_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Unable to create virtual IO RX task");
        close(sock);
        s_ctx.sock = -1;
        return ESP_OK; // non-fatal
    }

    ESP_LOGI(TAG, "Virtual IO state bus listening on UDP port %d", VIRTUAL_IO_UDP_PORT);
    return ESP_OK;
}
