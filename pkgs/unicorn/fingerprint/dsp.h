/* SPDX-License-Identifier: MIT */
#ifndef UF_DSP_H
#define UF_DSP_H
#include <stdbool.h>
#include <stdint.h>
struct uf_dsp;
/* The process owns one sensor and one DSP proxy. Calls are serialized by its
 * action worker. The parent can terminate the worker if a kernel RPC blocks. */
/* Start returns before RPC initialization; keep DMA alive until close returns. */
struct uf_dsp *uf_dsp_start(int private_dma_fd, uint32_t chip, int cancellation_fd);
int uf_dsp_ready(struct uf_dsp *, int cancellation_fd, int timeout_ms);
int uf_dsp_speed(struct uf_dsp *, bool high, int cancellation_fd);
int uf_dsp_begin(struct uf_dsp *, unsigned command);
int uf_dsp_wait(struct uf_dsp *, int cancellation_fd, int timeout_ms);
int uf_dsp_close(struct uf_dsp *);
int uf_dsp_notify(uint32_t command, int32_t result);
#endif
