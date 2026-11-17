/* SPDX-License-Identifier: MIT */
#ifndef UF_SELECTION_H
#define UF_SELECTION_H
/* Cache affects ordering only. The callback must fully initialize and release. */
const char *uf_select(const char *cached, int (*qualify)(const char *, void *), void *data);
int uf_cache_read(const char *directory, char candidate[8]);
int uf_cache_write(const char *directory, const char *candidate);
#endif
