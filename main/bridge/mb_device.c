#include "mb_device.h"
#include "modbus_helpers.h"

#include "sys_info.h"
#include "wb_app_desc.h"
#include "voltage_monitor.h"
#include "setting_items.h"
#include "cache_multimaster.h"

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- Modbus exception codes (aliases of the canonical modbus_helpers set) - */

#define MB_DEV_EX_ILLEGAL_FUNCTION   MODBUS_EXC_ILLEGAL_FUNCTION
#define MB_DEV_EX_ILLEGAL_ADDRESS    MODBUS_EXC_ILLEGAL_ADDRESS
#define MB_DEV_EX_ILLEGAL_DATA_VALUE MODBUS_EXC_ILLEGAL_DATA_VALUE

/* ---- Modbus function codes (aliases of the canonical modbus_helpers set) -- */

#define MB_DEV_FC_READ_HOLDING_REGS  MODBUS_FC_READ_HOLDING_REGS
#define MB_DEV_FC_READ_INPUT_REGS    MODBUS_FC_READ_INPUT_REGS

/* ---- Modbus limits ------------------------------------------------------- */

#define MB_DEV_MAX_REGISTERS         125u  /* max registers per FC03/FC04 read */

/* ---- Input register address map (FC04) ----------------------------------- */

#define REG_UPTIME_HI         104u  /* 0x0068 */
#define REG_UPTIME_LO         105u  /* 0x0069 */
#define REG_SUPPLY_VOLTAGE    121u  /* 0x0079 */

#define REG_MODEL_BASE        200u  /* 0x00C8 .. 0x00DB, 20 regs */
#define REG_MODEL_COUNT        20u
#define REG_GIT_BASE          220u  /* 0x00DC .. 0x00F4, 25 regs */
#define REG_GIT_COUNT          25u
#define REG_FWVER_BASE        250u  /* 0x00FA .. 0x0109, 16 regs */
#define REG_FWVER_COUNT        16u

#define REG_SERIAL_EXT_BASE   266u  /* 0x010A .. 0x010D, u64 MSW-first */
#define REG_SERIAL_BASE       270u  /* 0x010E .. 0x010F, u32 MSW-first */

#define REG_FW_MAJOR          320u  /* 0x0140 */
#define REG_FW_MINOR          321u  /* 0x0141 */
#define REG_FW_PATCH          322u  /* 0x0142 */
#define REG_FW_SUFFIX         323u  /* 0x0143 */
#define REG_FW_VERSION_LE_LO  324u  /* 0x0144 little-endian word order, low word  */
#define REG_FW_VERSION_LE_HI  325u  /* 0x0145 little-endian word order, high word */
#define REG_FW_VERSION_BE_HI  326u  /* 0x0146 big-endian word order, high word    */
#define REG_FW_VERSION_BE_LO  327u  /* 0x0147 big-endian word order, low word     */

#define REG_CACHE_TIMEOUT     336u  /* 0x0150 */
#define REG_PKT_PROC_HI       337u  /* 0x0151 */
#define REG_PKT_PROC_LO       338u  /* 0x0152 */
#define REG_LAST_PKT_AGE_HI   339u  /* 0x0153 */
#define REG_LAST_PKT_AGE_LO   340u  /* 0x0154 */
#define REG_DEVICES_ON_BUS    341u  /* 0x0155 */
#define REG_POLL_FREQ_PPM     342u  /* 0x0156 */

#define REG_MAX_STACK_USED    65504u /* 0xFFE0 */
#define REG_FREE_RAM          65505u /* 0xFFE1 */
#define REG_USED_RAM          65506u /* 0xFFE2 */
#define REG_STACK_SIZE        65507u /* 0xFFE3 */
#define REG_REBOOT_REASON     65508u /* 0xFFE4 */

/* ---- Holding register address map (FC03) --------------------------------- */

#define REG_SIGNATURE_BASE    290u  /* 0x0122 .. 0x012D, 12 regs */
#define REG_SIGNATURE_COUNT    12u

/* ---- WB reboot reason codes ---------------------------------------------- */

#define WB_REBOOT_NONE  0u
#define WB_REBOOT_LPWR  1u  /* brownout / deep sleep wakeup */
#define WB_REBOOT_WWDG  2u  /* interrupt watchdog          */
#define WB_REBOOT_IWDG  3u  /* task / generic watchdog     */
#define WB_REBOOT_SFT   4u  /* software / panic            */
#define WB_REBOOT_POR   5u  /* power-on                    */
#define WB_REBOOT_PIN   6u  /* external reset pin          */

/* ---- Helpers ------------------------------------------------------------- */

/* Saturate a 32-bit value to a 16-bit register (cap at 0xFFFF). */
static uint16_t sat_u16(uint32_t v)
{
    return (v > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)v;
}

/*
 * Pack two consecutive characters of a NUL-terminated string into a single
 * 16-bit register, big-endian (high byte = first character). Indices past the
 * end of the string (or past maxlen) are zero-padded.
 *
 *   base : base register address of the string field
 *   addr : the requested register address (base <= addr < base + field_regs)
 *   maxlen : maximum field length in characters (string buffer bound)
 */
static uint16_t pack_string_reg(const char *s, size_t maxlen, uint16_t base, uint16_t addr)
{
    size_t i = (size_t)(addr - base) * 2u;
    size_t slen = strlen(s);

    uint8_t hi = 0;
    uint8_t lo = 0;
    if (i < slen && i < maxlen) {
        hi = (uint8_t)s[i];
    }
    if ((i + 1u) < slen && (i + 1u) < maxlen) {
        lo = (uint8_t)s[i + 1u];
    }
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

/* Parsed firmware version, derived from sys_info.firmware_ver. */
typedef struct {
    unsigned major;
    unsigned minor;
    unsigned patch;
    int      suffix;   /* +N for "+wbN", -N for "-rcN", 0 if none */
    uint32_t version;  /* encoded numeric version (see WB wiki)   */
} fw_version_t;

/* Parse sys_info.firmware_ver into numeric components and the encoded version. */
static void parse_fw_version(fw_version_t *out)
{
    unsigned major = 0, minor = 0, patch = 0;
    int suffix = 0;

    sscanf(sys_info.firmware_ver, "%u.%u.%u", &major, &minor, &patch);
    const char *plus = strstr(sys_info.firmware_ver, "+wb");
    const char *rc   = strstr(sys_info.firmware_ver, "-rc");
    if (plus) {
        suffix = atoi(plus + 3);
    } else if (rc) {
        suffix = -atoi(rc + 3);
    }

    /* Numeric version per Wiren Board wiki:
     * https://wiki.wirenboard.com/wiki/Modbus-hardware-version */
    unsigned enc = (suffix >= 0) ? (unsigned)(suffix + 128) : (unsigned)(-1 - suffix);
    uint32_t version = ((uint32_t)(major & 0xFFu) << 24) |
                       ((uint32_t)(minor & 0xFFu) << 16) |
                       ((uint32_t)(patch & 0xFFu) <<  8) |
                       (enc & 0xFFu);

    out->major   = major;
    out->minor   = minor;
    out->patch   = patch;
    out->suffix  = suffix;
    out->version = version;
}

/* Map esp_reset_reason() to a WB reboot reason code. */
static uint16_t map_reboot_reason(void)
{
    switch (esp_reset_reason()) {
        case ESP_RST_BROWNOUT:  return WB_REBOOT_LPWR;
        case ESP_RST_DEEPSLEEP: return WB_REBOOT_LPWR;
        case ESP_RST_INT_WDT:   return WB_REBOOT_WWDG;
        case ESP_RST_TASK_WDT:  return WB_REBOOT_IWDG;
        case ESP_RST_WDT:       return WB_REBOOT_IWDG;
        case ESP_RST_SW:        return WB_REBOOT_SFT;
        case ESP_RST_PANIC:     return WB_REBOOT_SFT;
        case ESP_RST_POWERON:   return WB_REBOOT_POR;
        case ESP_RST_EXT:       return WB_REBOOT_PIN;
        default:                return WB_REBOOT_NONE;
    }
}

/* ---- Register getters ---------------------------------------------------- */

/*
 * Look up one input register (FC04) by address.
 * task_stack_bytes: total stack size of the calling task, for the stack-usage
 * registers (0 if unknown).
 * Returns true and writes *val if the address is a defined register, false otherwise.
 */
static bool device_get_input_reg(uint16_t addr, uint16_t task_stack_bytes, uint16_t *val)
{
    /* uptime, 32-bit, MSW-first */
    if (addr == REG_UPTIME_HI || addr == REG_UPTIME_LO) {
        uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
        *val = (addr == REG_UPTIME_HI) ? (uint16_t)(uptime_s >> 16)
                                       : (uint16_t)(uptime_s & 0xFFFFu);
        return true;
    }

    /* supply voltage in mV */
    if (addr == REG_SUPPLY_VOLTAGE) {
        *val = (uint16_t)(voltage_monitor_get_sys_voltage() * 1000.0f + 0.5f);
        return true;
    }

    /* device model string */
    if (addr >= REG_MODEL_BASE && addr < REG_MODEL_BASE + REG_MODEL_COUNT) {
        *val = pack_string_reg(sys_info.device_name, DEVICE_MODEL_LEN,
                               REG_MODEL_BASE, addr);
        return true;
    }

    /* firmware git info string */
    if (addr >= REG_GIT_BASE && addr < REG_GIT_BASE + REG_GIT_COUNT) {
        *val = pack_string_reg(sys_info.firmware_git_info, FIRMWARE_GIT_INFO_LEN,
                               REG_GIT_BASE, addr);
        return true;
    }

    /* firmware version string */
    if (addr >= REG_FWVER_BASE && addr < REG_FWVER_BASE + REG_FWVER_COUNT) {
        *val = pack_string_reg(sys_info.firmware_ver, FIRMWARE_VERSION_LEN,
                               REG_FWVER_BASE, addr);
        return true;
    }

    /* serial extension: u64, MSW-first across 266..269 */
    if (addr >= REG_SERIAL_EXT_BASE && addr <= REG_SERIAL_EXT_BASE + 3u) {
        uint64_t sn = sys_info.device_serial_num;
        unsigned shift = (3u - (unsigned)(addr - REG_SERIAL_EXT_BASE)) * 16u;
        *val = (uint16_t)((sn >> shift) & 0xFFFFu);
        return true;
    }

    /* serial number: low u32, MSW-first across 270..271 */
    if (addr == REG_SERIAL_BASE || addr == REG_SERIAL_BASE + 1u) {
        uint32_t sn = (uint32_t)(sys_info.device_serial_num & 0xFFFFFFFFu);
        *val = (addr == REG_SERIAL_BASE) ? (uint16_t)(sn >> 16)
                                         : (uint16_t)(sn & 0xFFFFu);
        return true;
    }

    /* firmware numeric version components */
    if (addr == REG_FW_MAJOR || addr == REG_FW_MINOR || addr == REG_FW_PATCH ||
        addr == REG_FW_SUFFIX ||
        addr == REG_FW_VERSION_LE_LO || addr == REG_FW_VERSION_LE_HI ||
        addr == REG_FW_VERSION_BE_HI || addr == REG_FW_VERSION_BE_LO) {
        fw_version_t fw;
        parse_fw_version(&fw);
        switch (addr) {
            case REG_FW_MAJOR: *val = (uint16_t)fw.major; break;
            case REG_FW_MINOR: *val = (uint16_t)fw.minor; break;
            case REG_FW_PATCH: *val = (uint16_t)fw.patch; break;
            case REG_FW_SUFFIX: *val = (uint16_t)(int16_t)fw.suffix; break;
            case REG_FW_VERSION_LE_LO: *val = (uint16_t)(fw.version & 0xFFFFu); break;
            case REG_FW_VERSION_LE_HI: *val = (uint16_t)((fw.version >> 16) & 0xFFFFu); break;
            case REG_FW_VERSION_BE_HI: *val = (uint16_t)((fw.version >> 16) & 0xFFFFu); break;
            case REG_FW_VERSION_BE_LO: *val = (uint16_t)(fw.version & 0xFFFFu); break;
            default: return false;
        }
        return true;
    }

    /* statistics block (aggregate) */
    if (addr == REG_CACHE_TIMEOUT) {
        *val = (uint16_t)setting_items_read_int(KEY_CACHE_VALUE_TIMEOUT_S);
        return true;
    }
    if (addr == REG_PKT_PROC_HI || addr == REG_PKT_PROC_LO ||
        addr == REG_LAST_PKT_AGE_HI || addr == REG_LAST_PKT_AGE_LO ||
        addr == REG_DEVICES_ON_BUS || addr == REG_POLL_FREQ_PPM) {
        cache_multimaster_stats_t st;
        cache_multimaster_get_stats(&st);
        switch (addr) {
            case REG_PKT_PROC_HI:     *val = (uint16_t)(st.packets_processed >> 16); break;
            case REG_PKT_PROC_LO:     *val = (uint16_t)(st.packets_processed & 0xFFFFu); break;
            case REG_LAST_PKT_AGE_HI: *val = (uint16_t)(st.last_packet_age_s >> 16); break;
            case REG_LAST_PKT_AGE_LO: *val = (uint16_t)(st.last_packet_age_s & 0xFFFFu); break;
            case REG_DEVICES_ON_BUS:  *val = st.devices_on_bus; break;
            case REG_POLL_FREQ_PPM:
                if (st.map_age_s > 0) {
                    uint64_t ppm = (uint64_t)st.packets_processed * 60u / st.map_age_s;
                    *val = (ppm > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)ppm;
                } else {
                    *val = 0;
                }
                break;
            default: return false;
        }
        return true;
    }

    /* RAM / stack diagnostics (reported in kilobytes, floor division by 1024) */
    if (addr == REG_MAX_STACK_USED) {
        uint16_t free_min = (uint16_t)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
        if (task_stack_bytes > 0 && task_stack_bytes >= free_min) {
            uint32_t used = (uint32_t)task_stack_bytes - (uint32_t)free_min;
            *val = sat_u16(used / 1024u); /* used stack in KB */
        } else {
            *val = 0; /* 0 means stack corrupted/unknown per the WB spec */
        }
        return true;
    }
    if (addr == REG_FREE_RAM) {
        uint32_t free_internal = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        *val = sat_u16(free_internal / 1024u); /* free internal RAM in KB */
        return true;
    }
    if (addr == REG_USED_RAM) {
        uint32_t total_internal = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        uint32_t free_internal  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        uint32_t used = (total_internal > free_internal) ? (total_internal - free_internal) : 0u;
        *val = sat_u16(used / 1024u); /* used internal RAM in KB */
        return true;
    }
    if (addr == REG_STACK_SIZE) {
        *val = sat_u16((uint32_t)task_stack_bytes / 1024u); /* total stack size in KB */
        return true;
    }
    if (addr == REG_REBOOT_REASON) {
        *val = map_reboot_reason();
        return true;
    }

    return false;
}

/*
 * Look up one holding register (FC03) by address.
 * Returns true and writes *val if the address is a defined register, false otherwise.
 */
static bool device_get_holding_reg(uint16_t addr, uint16_t *val)
{
    /* device signature string */
    if (addr >= REG_SIGNATURE_BASE && addr < REG_SIGNATURE_BASE + REG_SIGNATURE_COUNT) {
        *val = pack_string_reg(sys_info.device_signature, DEVICE_SIGNATURE_LEN,
                               REG_SIGNATURE_BASE, addr);
        return true;
    }
    return false;
}

/* ---- Public API ---------------------------------------------------------- */

bool mb_device_is_self(uint8_t unit_id)
{
    return unit_id == MB_DEVICE_UNIT_ID;
}

size_t mb_device_build_read_response(uint8_t unit_id, uint8_t fc,
                                     uint16_t transaction_id_net,
                                     uint16_t start_addr, uint16_t count,
                                     uint16_t task_stack_size_bytes,
                                     uint8_t *resp_buf, uint8_t *exc_out)
{
    if (fc != MB_DEV_FC_READ_HOLDING_REGS && fc != MB_DEV_FC_READ_INPUT_REGS) {
        *exc_out = MB_DEV_EX_ILLEGAL_FUNCTION;
        return 0;
    }

    /* Reject ranges that overflow the 16-bit address space. */
    if ((uint32_t)start_addr + (uint32_t)count > 0x10000u) {
        *exc_out = MB_DEV_EX_ILLEGAL_ADDRESS;
        return 0;
    }

    mb_tcp_header_t *resp_hdr = (mb_tcp_header_t *)resp_buf;
    resp_hdr->transaction_id = transaction_id_net; /* echoed verbatim (network order) */
    resp_hdr->protocol_id    = 0x0000;
    resp_hdr->unit_id        = unit_id;
    resp_hdr->function       = fc;

    uint8_t *payload    = resp_buf + sizeof(mb_tcp_header_t);
    uint8_t  byte_count = (uint8_t)(count * 2u);
    payload[0] = byte_count;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t addr  = (uint16_t)(start_addr + i);
        uint16_t value = 0;
        bool found = (fc == MB_DEV_FC_READ_INPUT_REGS)
                     ? device_get_input_reg(addr, task_stack_size_bytes, &value)
                     : device_get_holding_reg(addr, &value);
        if (!found) {
            /* Any undefined register in the range -> ILLEGAL DATA ADDRESS. */
            *exc_out = MB_DEV_EX_ILLEGAL_ADDRESS;
            return 0;
        }
        payload[1 + i * 2]     = (uint8_t)(value >> 8);
        payload[1 + i * 2 + 1] = (uint8_t)(value & 0xFFu);
    }

    /* MBAP length = unit_id(1) + FC(1) + byte_count_field(1) + data(byte_count) */
    uint16_t pdu_len = (uint16_t)(1u + 1u + 1u + byte_count);
    resp_hdr->length = modbus_swap16(pdu_len);

    return sizeof(mb_tcp_header_t) + 1u + (size_t)byte_count;
}

size_t mb_device_handle_self_request(const uint8_t *req, size_t req_len,
                                     uint16_t task_stack_size_bytes,
                                     uint8_t *resp_buf)
{
    const mb_tcp_header_t *req_hdr = (const mb_tcp_header_t *)req;
    uint16_t tid     = req_hdr->transaction_id; /* network byte order, echoed verbatim */
    uint8_t  unit_id = req_hdr->unit_id;
    uint8_t  fc      = req_hdr->function;

    if (fc != MB_DEV_FC_READ_HOLDING_REGS && fc != MB_DEV_FC_READ_INPUT_REGS) {
        return modbus_pdu_build_exception(resp_buf, tid, unit_id, fc, MB_DEV_EX_ILLEGAL_FUNCTION);
    }
    if (req_len < sizeof(mb_tcp_header_t) + 4u) {
        return modbus_pdu_build_exception(resp_buf, tid, unit_id, fc, MB_DEV_EX_ILLEGAL_DATA_VALUE);
    }

    const uint8_t *pdu = req + sizeof(mb_tcp_header_t);
    uint16_t start = 0;
    uint16_t count = 0;
    modbus_pdu_parse_read_request(pdu, &start, &count);
    if (count < 1u || count > MB_DEV_MAX_REGISTERS) {
        return modbus_pdu_build_exception(resp_buf, tid, unit_id, fc, MB_DEV_EX_ILLEGAL_DATA_VALUE);
    }

    uint8_t exc = MB_DEV_EX_ILLEGAL_ADDRESS;
    size_t rlen = mb_device_build_read_response(unit_id, fc, tid, start, count,
                                                task_stack_size_bytes, resp_buf, &exc);
    if (rlen == 0) {
        return modbus_pdu_build_exception(resp_buf, tid, unit_id, fc, exc);
    }
    return rlen;
}
