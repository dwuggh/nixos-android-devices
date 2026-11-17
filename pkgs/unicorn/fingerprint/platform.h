/* SPDX-License-Identifier: MIT */
#ifndef UF_PLATFORM_H
#define UF_PLATFORM_H
#include <stddef.h>
/* Read-only dependency check, before any sensor reset or trusted-app load. */
int uf_platform_check(char *detail, size_t size);
#endif
