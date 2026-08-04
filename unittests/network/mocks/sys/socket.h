#pragma once

// network.c includes sys/socket.h for exactly two things: AF_INET and inet_pton(), which on
// the target come from lwip's BSD socket layer. The host's own <sys/socket.h> cannot be
// reached from a file with the same name, so both are declared here; inet_pton itself is
// left to libc, whose dotted-quad parsing is what str_to_ip() relies on.

#define AF_INET 2

int inet_pton(int af, const char *src, void *dst);
