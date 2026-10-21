/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_L4S_RSSI_H
#define APP_L4S_RSSI_H
#include "l4s_protocol.h"

bool L4S_RssiDue(const L4S_State *state, uint32_t now, bool transmitting);
void L4S_UpdateRssi(L4S_State *state, uint32_t now, uint16_t raw,
                    int16_t correction_db, const uint16_t calibration[4]);
#endif
