#include "array_size.h"
#include <stdint.h>

static const uint8_t favicon_data[] = {
    0x47, 0x49, 0x46, 0x38, 0x00, 0x3B
};

static const uint8_t css_data[] = {
    0x1F, 0x8B, 0x08, 0x00
};

static const uint8_t js_data[] = {
    0x5F, 0x1B, 0x10, 0x03
};

static const uint8_t html_data[] = {
    0xFB, 0x23, 0x54, 0x99
};

const uint8_t * binary_favicon_webp_gz_start = favicon_data;
const uint8_t * binary_favicon_webp_gz_end = favicon_data + ARRAY_SIZE(favicon_data);

const uint8_t * binary_index_css_gz_start = css_data;
const uint8_t * binary_index_css_gz_end = css_data + ARRAY_SIZE(css_data);

const uint8_t * binary_index_js_gz_start = js_data;
const uint8_t * binary_index_js_gz_end = js_data + ARRAY_SIZE(js_data);

const uint8_t * binary_index_html_gz_start = html_data;
const uint8_t * binary_index_html_gz_end = html_data + ARRAY_SIZE(html_data);

const uint8_t * _binary_favicon_webp_gz_start = favicon_data;
const uint8_t * _binary_favicon_webp_gz_end = favicon_data + ARRAY_SIZE(favicon_data);

const uint8_t * _binary_index_css_gz_start = css_data;
const uint8_t * _binary_index_css_gz_end = css_data + ARRAY_SIZE(css_data);

const uint8_t * _binary_index_js_gz_start = js_data;
const uint8_t * _binary_index_js_gz_end = js_data + ARRAY_SIZE(js_data);

const uint8_t * _binary_index_html_gz_start = html_data;
const uint8_t * _binary_index_html_gz_end = html_data + ARRAY_SIZE(html_data);
