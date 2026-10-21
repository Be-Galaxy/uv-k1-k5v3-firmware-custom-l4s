/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#ifndef HELPER_RSSI_H
#define HELPER_RSSI_H
#include <stdint.h>

static inline int16_t RSSI_RawToDbm(uint16_t raw)
{
    return (int16_t)(raw / 2u) - 160;
}

/* Match the native F4HWN S-meter's strong-signal display ceiling. */
static inline int16_t RSSI_DisplayDbm(int16_t calibrated_dbm)
{
    return calibrated_dbm > -53 ? -53 : calibrated_dbm;
}
#endif
