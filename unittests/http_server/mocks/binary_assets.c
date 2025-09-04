#include <stdint.h>

// На хост-платформах при сборке через GCC префикс _ не требуется
const uint8_t binary_favicon_webp_gz_start[] = {0x47, 0x49, 0x46, 0x38};
const uint8_t binary_favicon_webp_gz_end[] = {0x00, 0x3B};
const uint8_t binary_index_css_gz_start[] = {0x1F, 0x8B, 0x08, 0x00};
const uint8_t binary_index_css_gz_end[] = {0x00, 0x00};
const uint8_t binary_index_js_gz_start[] = {0x1F, 0x8B, 0x08, 0x00};
const uint8_t binary_index_js_gz_end[] = {0x00, 0x00};
const uint8_t binary_index_html_gz_start[] = {0x1F, 0x8B, 0x08, 0x00};
const uint8_t binary_index_html_gz_end[] = {0x00, 0x00};

// Для Jenkins сервера требуется префикс _
const uint8_t _binary_favicon_webp_gz_start[] = {0x47, 0x49, 0x46, 0x38};
const uint8_t _binary_favicon_webp_gz_end[] = {0x00, 0x3B};
const uint8_t _binary_index_css_gz_start[] = {0x1F, 0x8B, 0x08, 0x00};
const uint8_t _binary_index_css_gz_end[] = {0x00, 0x00};
const uint8_t _binary_index_js_gz_start[] = {0x1F, 0x8B, 0x08, 0x00};
const uint8_t _binary_index_js_gz_end[] = {0x00, 0x00};
const uint8_t _binary_index_html_gz_start[] = {0x1F, 0x8B, 0x08, 0x00};
const uint8_t _binary_index_html_gz_end[] = {0x00, 0x00};
