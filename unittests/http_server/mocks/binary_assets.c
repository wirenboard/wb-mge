#include <stdint.h>

#include "sys_info.h"

/* Roboto Latin subset mock — embedded RAW woff2 data (no gzip) */
static const uint8_t roboto_latin_data[] = {
    0x77, 0x4F, 0x46, 0x32
};

/* Roboto Cyrillic subset mock — embedded RAW woff2 data (no gzip) */
static const uint8_t roboto_cyrillic_data[] = {
    0x77, 0x4F, 0x46, 0x32
};

/* Roboto Cyrillic-ext subset mock — embedded RAW woff2 data (no gzip) */
static const uint8_t roboto_cyrillic_ext_data[] = {
    0x77, 0x4F, 0x46, 0x32
};

static const uint8_t favicon_data[] = {
    0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50
};

static const uint8_t css_data[] = {
    0x1F, 0x8B, 0x08, 0x00, 0x55, 0xC3, 0x9A
};

static const uint8_t js_data[] = {
    0x5F, 0x1B, 0x10, 0x4D, 0xB8, 0x2A
};

static const uint8_t html_data[] = {
    0xFB, 0x23, 0x6D, 0x91, 0x3C
};

/* favicon and fonts are embedded RAW (no _gz suffix) to match EMBED_FILES */
const uint8_t * favicon_start asm("_binary_favicon_webp_start") = favicon_data;
const uint8_t * favicon_end asm("_binary_favicon_webp_end") = favicon_data + sizeof(favicon_data);

const uint8_t * index_css_start asm("_binary_index_css_gz_start") = css_data;
const uint8_t * index_css_end asm("_binary_index_css_gz_end") = css_data + sizeof(css_data);

const uint8_t * index_js_start asm("_binary_index_js_gz_start") = js_data;
const uint8_t * index_js_end asm("_binary_index_js_gz_end") = js_data + sizeof(js_data);

const uint8_t * index_html_start asm("_binary_index_html_gz_start") = html_data;
const uint8_t * index_html_end asm("_binary_index_html_gz_end") = html_data + sizeof(html_data);

const uint8_t * roboto_latin_start    asm("_binary_roboto_latin_wght_normal_woff2_start")    = roboto_latin_data;
const uint8_t * roboto_latin_end      asm("_binary_roboto_latin_wght_normal_woff2_end")      = roboto_latin_data    + sizeof(roboto_latin_data);
const uint8_t * roboto_cyrillic_start asm("_binary_roboto_cyrillic_wght_normal_woff2_start") = roboto_cyrillic_data;
const uint8_t * roboto_cyrillic_end   asm("_binary_roboto_cyrillic_wght_normal_woff2_end")   = roboto_cyrillic_data + sizeof(roboto_cyrillic_data);
const uint8_t * roboto_cyrillic_ext_start asm("_binary_roboto_cyrillic_ext_wght_normal_woff2_start") = roboto_cyrillic_ext_data;
const uint8_t * roboto_cyrillic_ext_end   asm("_binary_roboto_cyrillic_ext_wght_normal_woff2_end")   = roboto_cyrillic_ext_data + sizeof(roboto_cyrillic_ext_data);

/* sys_info backing store for the ETag derivation in http_server_init(). */
sys_info_t sys_info = {
    .firmware_ver      = "test-fw",
    .firmware_git_info = "test-build-1",
};
