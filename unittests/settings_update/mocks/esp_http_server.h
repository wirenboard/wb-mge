#pragma once

// settings_update.c includes the real main/wb_test.h — a quoted include resolves against
// the includer's own directory first, so the mocks/wb_test.h next door cannot shadow it —
// and that header declares the two httpd handlers next to the clock_out guard this suite
// actually needs. Only the opaque request type has to exist for it to compile; there is no
// web server anywhere in these tests.

typedef struct httpd_req httpd_req_t;
