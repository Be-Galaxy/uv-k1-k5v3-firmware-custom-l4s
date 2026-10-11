/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_L4S_IO_H
#define APP_L4S_IO_H
#include "l4s_protocol.h"
#include "driver/keyboard.h"
typedef struct {
    uint8_t data[L4S_MAX_BODY+8];
    volatile bool busy;
} L4S_ReplyBuffer;
bool L4S_SendReply(L4S_ReplyBuffer *buffer, bool dtr, const uint8_t *data, size_t size,
                   int (*submit)(const uint8_t *, size_t));
void L4S_ReplyComplete(L4S_ReplyBuffer *buffer);
bool L4S_FilterKey(L4S_State *state, KEY_Code_t key, bool pressed, bool *release_latch);
#endif
