#pragma once
/*
 * WB device template parser.
 * Reads a wb-mqtt-serial JSON template and extracts the channel list
 * that we need to poll / publish.
 *
 * Supports channel `condition` evaluation against the device `parameters`
 * section (value/default). Still no Jinja, no consists_of, no setup.
 *
 * Designed to be buildable on both Linux (PoC) and an MCU (ESP32).
 * Heap usage: one call to malloc per channel name string + the
 * channels array itself.  On an MCU with 700 kB RAM this is fine.
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>  /* NAN */

/* ------------------------------------------------------------------ */
/* Register types (Modbus function codes used for read/write)          */
/* ------------------------------------------------------------------ */
typedef enum {
    REG_HOLDING  = 0,  /* FC03 read, FC06/16 write */
    REG_INPUT    = 1,  /* FC04 read, read-only      */
    REG_COIL     = 2,  /* FC01 read, FC05 write     */
    REG_DISCRETE = 3,  /* FC02 read, read-only      */
} reg_type_t;

/* ------------------------------------------------------------------ */
/* Register data formats                                               */
/* ------------------------------------------------------------------ */
typedef enum {
    FMT_U8    = 0,
    FMT_S8    = 1,
    FMT_U16   = 2,
    FMT_S16   = 3,
    FMT_U32   = 4,
    FMT_S32   = 5,
    FMT_U64   = 6,
    FMT_S64   = 7,
    FMT_FLOAT  = 8,
    FMT_STRING = 9,   /* ASCII string in holding registers, big-endian bytes */
    FMT_BCD8  = 10,   /* 1 byte  = 2 BCD digits, e.g. 0x99 -> 99            */
    FMT_BCD16 = 11,   /* 2 bytes = 4 BCD digits (1 register)                */
    FMT_BCD24 = 12,   /* 3 bytes = 6 BCD digits (2 registers, 1 unused byte)*/
    FMT_BCD32 = 13,   /* 4 bytes = 8 BCD digits (2 registers)               */
} reg_format_t;

/* ------------------------------------------------------------------ */
/* Word/byte order enums                                               */
/* ------------------------------------------------------------------ */
typedef enum {
    CH_WORD_ORDER_BIG    = 0,  /* regs[0] = MSW (default) */
    CH_WORD_ORDER_LITTLE = 1,  /* regs[0] = LSW           */
} ch_word_order_t;

typedef enum {
    CH_BYTE_ORDER_BIG    = 0,  /* hi byte first in each word (default) */
    CH_BYTE_ORDER_LITTLE = 1,  /* lo byte first in each word           */
} ch_byte_order_t;

/* ------------------------------------------------------------------ */
/* A single enum value->label mapping entry                            */
/* ------------------------------------------------------------------ */
typedef struct {
    long   value;   /* raw register value */
    char  *title;   /* heap-allocated display label */
} wb_enum_entry_t;

/* ------------------------------------------------------------------ */
/* A single channel extracted from the template                        */
/* ------------------------------------------------------------------ */
typedef struct {
    char             *name;             /* channel name, heap-allocated           */
    reg_type_t        reg_type;         /* holding / input / coil / discrete      */
    uint32_t          address;          /* register address                       */
    reg_format_t      format;           /* data format for multi-word registers   */
    double            scale;            /* multiplier (default 1.0)               */
    double            offset;           /* additive offset (default 0.0)          */
    double            error_value;      /* NaN = disabled; raw match -> "Error"   */
    bool              readonly;         /* true for input/discrete or explicit    */
    bool              enabled;          /* false = skip this channel              */
    uint32_t          num_regs;         /* number of 16-bit Modbus registers      */
    uint32_t          string_data_size; /* for FMT_STRING: bytes in the string    */
    ch_word_order_t   word_order;       /* word order for multi-register values   */
    ch_byte_order_t   byte_order;       /* byte order within each word            */
    wb_enum_entry_t  *enums;      /* heap array of value->label maps, NULL if none */
    int               enum_count; /* number of enum entries (0 if none)            */
} wb_channel_t;

/* ------------------------------------------------------------------ */
/* Parsed template                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    char         device_name[64];   /* device.name from template           */
    char         device_id[64];     /* device.id  from template            */
    wb_channel_t *channels;         /* heap-allocated array                */
    int           num_channels;     /* length of the array                 */
} wb_template_t;

/*
 * Parse a wb-mqtt-serial JSON template file.
 * Returns 0 on success, -1 on error.
 * On success *out is filled; caller must call wb_template_free() when done.
 */
int  wb_template_parse(const char *path, wb_template_t *out);

/* Free all resources owned by *t.  Does not free t itself. */
void wb_template_free(wb_template_t *t);

/* Return the label mapped to `value` for an enum channel, or NULL if the
 * channel has no enum or the value is not listed. */
const char *wb_channel_enum_title(const wb_channel_t *ch, long value);

/* If `title` matches one of the channel's enum labels, store its raw value in
 * *out_value and return true; otherwise return false. */
bool wb_channel_enum_value(const wb_channel_t *ch, const char *title, long *out_value);
