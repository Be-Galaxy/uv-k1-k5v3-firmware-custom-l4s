/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_L4S_CONTROL_H
#define APP_L4S_CONTROL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "driver/keyboard.h"
#include "l4s_protocol.h"
void L4S_ControlTick(void);
bool L4S_Active(void);
bool L4S_BlockPhysicalPtt(void);
bool L4S_LocalKey(bool pressed);
void L4S_UsbLost(void);
bool L4S_HandleUart(uint32_t port, const uint8_t *body, size_t size);
const L4S_State *L4S_GetState(void);
void L4S_ProcessKeys(KEY_Code_t key, bool pressed, bool held);
#endif
