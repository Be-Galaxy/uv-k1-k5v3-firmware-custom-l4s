/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_io.h"
#include <string.h>
bool L4S_SendReply(L4S_ReplyBuffer *buffer, bool dtr, const uint8_t *data, size_t size,
                   int (*submit)(const uint8_t *, size_t))
{
    if (!dtr || buffer->busy || !size || size>sizeof(buffer->data)) return false;
    memcpy(buffer->data,data,size);
    buffer->busy=true;
    if (submit(buffer->data,size)<0) { buffer->busy=false; return false; }
    return true;
}
void L4S_ReplyComplete(L4S_ReplyBuffer *buffer) { buffer->busy=false; }
bool L4S_FilterKey(L4S_State *state, KEY_Code_t key, bool pressed, bool *release_latch)
{
    if (*release_latch) {
        if (!pressed) *release_latch=false;
        return true;
    }
    if (!state->session) return false;
    if (key==KEY_EXIT && pressed) {
        *release_latch=true;
        L4S_Abort(state,L4S_CANCELLED);
    }
    return true; /* Consume other keys; physical PTT has its own path. */
}
