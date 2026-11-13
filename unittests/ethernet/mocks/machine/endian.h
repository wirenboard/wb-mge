/*
 * Host shim for <machine/endian.h>, same as unittests/network/mocks/machine/endian.h.
 *
 * esp_netif_ip_addr.h includes <machine/endian.h> unconditionally and then compares
 * BYTE_ORDER against BIG_ENDIAN. That header exists on macOS but not in the glibc
 * container CI builds in, so the BSD names are derived from the compiler's own
 * byte-order macros.
 */
#pragma once

#ifndef BYTE_ORDER
#define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#define BIG_ENDIAN __ORDER_BIG_ENDIAN__
#define BYTE_ORDER __BYTE_ORDER__
#endif
