#include <stdint.h>

static const uint8_t inter_latin_data[] = {
    0x1F, 0x8B, 0x08, 0x00
};

static const uint8_t inter_cyrillic_data[] = {
    0x1F, 0x8B, 0x08, 0x00
};

static const uint8_t favicon_data[] = {
    0x47, 0x49, 0x46, 0x38, 0x00, 0x3B, 0xA2, 0x7C, 0xE1
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

const uint8_t * favicon_start asm("_binary_favicon_webp_gz_start") = favicon_data;
const uint8_t * favicon_end asm("_binary_favicon_webp_gz_end") = favicon_data + sizeof(favicon_data);

const uint8_t * index_css_start asm("_binary_index_css_gz_start") = css_data;
const uint8_t * index_css_end asm("_binary_index_css_gz_end") = css_data + sizeof(css_data);

const uint8_t * index_js_start asm("_binary_index_js_gz_start") = js_data;
const uint8_t * index_js_end asm("_binary_index_js_gz_end") = js_data + sizeof(js_data);

const uint8_t * index_html_start asm("_binary_index_html_gz_start") = html_data;
const uint8_t * index_html_end asm("_binary_index_html_gz_end") = html_data + sizeof(html_data);

const uint8_t * inter_latin_start asm("_binary_inter_latin_woff2_gz_start") = inter_latin_data;
const uint8_t * inter_latin_end asm("_binary_inter_latin_woff2_gz_end") = inter_latin_data + sizeof(inter_latin_data);

const uint8_t * inter_cyrillic_start asm("_binary_inter_cyrillic_woff2_gz_start") = inter_cyrillic_data;
const uint8_t * inter_cyrillic_end asm("_binary_inter_cyrillic_woff2_gz_end") = inter_cyrillic_data + sizeof(inter_cyrillic_data);
