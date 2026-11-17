/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * Public FastRPC remote.h ABI (Qualcomm v1.0.7), declarations used here. */
#ifndef UF_RPC_ABI_H
#define UF_RPC_ABI_H
#include <stddef.h>
#include <stdint.h>
typedef uint64_t remote_handle64;
typedef union {
    struct {
        void *pv;
        size_t nLen;
    } buf;
    uint32_t h;
    uint64_t h64;
    struct {
        int32_t fd;
        uint32_t offset;
    } dma;
} remote_arg;
#endif
