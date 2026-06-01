#ifndef COVERAGE_DUMP_H
#define COVERAGE_DUMP_H

#include "esp_http_server.h"

// Registers the GET /gcov coverage-dump endpoint. Only meaningful in coverage
// builds (COVERAGE_BUILD); the implementation is compiled out otherwise.
void coverage_dump_register_handlers(httpd_handle_t server);

#endif /* COVERAGE_DUMP_H */
