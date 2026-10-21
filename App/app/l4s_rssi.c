/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_rssi.h"
#include "helper/rssi.h"

bool L4S_RssiDue(const L4S_State *s, uint32_t now, bool transmitting)
{
    return s->session && !transmitting &&
        (s->rssi_dbm == L4S_RSSI_UNKNOWN || (uint32_t)(now - s->rssi_sampled_at) >= 100u);
}

void L4S_UpdateRssi(L4S_State *s, uint32_t now, uint16_t raw,
                    int16_t correction_db, const uint16_t calibration[4])
{
    /* Match the native S-meter: the RF register uses half-dB steps. */
    s->rssi_dbm = RSSI_RawToDbm(raw) + correction_db;
    s->rssi_sampled_at = now;
    s->rssi = raw >= calibration[3] ? 6 : raw >= calibration[2] ? 4 :
              raw >= calibration[1] ? 2 : raw >= calibration[0] ? 1 : 0;
}
