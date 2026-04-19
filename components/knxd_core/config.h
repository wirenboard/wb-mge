/* config.h for ESP32 build — replaces autotools-generated config.h */
#ifndef CONFIG_H
#define CONFIG_H

#define PACKAGE_VERSION "0.14.74-esp32"
#define HAVE_SYS_TIME_H 1
#define HAVE_IPSECURE 1
/* #undef HAVE_OPENSSL */
#define HAVE_TPUART 1
#define HAVE_EIBNETIPTUNNEL 1
#define HAVE_BUSMONITOR 1
#define HAVE_FMT_PRINTF 0
/* #undef HAVE_MANAGEMENT */
/* #undef HAVE_GROUPCACHE */
/* #undef HAVE_SYSTEMD */
/* #undef HAVE_LINUX_LOWLATENCY */
/* #undef HAVE_LINUX_NETLINK */
/* #undef HAVE_WINDOWS_IPHELPER */
/* #undef HAVE_BSD_SOURCEINFO */
/* #undef HAVE_SOCKADDR_IN_LEN */
/* #undef HAVE_SA_SIZE */

#define ESP_PLATFORM 1

/* ESP32: no Unix domain sockets */
#ifndef AF_LOCAL
#define AF_LOCAL 0
#endif

/* ESP32: stub for IFHWADDRLEN */
#ifndef IFHWADDRLEN
#define IFHWADDRLEN 6
#endif

/* ESP32: if_nametoindex declared in lwIP headers but not implemented */

#endif
