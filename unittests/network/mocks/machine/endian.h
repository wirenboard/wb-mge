/*
 * Host shim for <machine/endian.h>.
 *
 * esp_netif_ip_addr.h includes <machine/endian.h> unconditionally and then
 * compares BYTE_ORDER against BIG_ENDIAN. That header is a BSD/macOS thing:
 * it exists on the developer's macOS box but not in the glibc container the
 * CI builds in, so the suite compiled locally and failed in CI on a missing
 * include. Deriving the BSD names from the compiler's own byte-order macros
 * keeps the shim host-agnostic instead of trading one platform for the other.
 */
#pragma once

#ifndef BYTE_ORDER
#define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#define BIG_ENDIAN __ORDER_BIG_ENDIAN__
#define BYTE_ORDER __BYTE_ORDER__
#endif
