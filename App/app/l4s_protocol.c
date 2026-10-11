/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_protocol.h"
#include <string.h>

static const uint8_t mask[16] = {0x16,0x6c,0x14,0xe6,0x2e,0x91,0x0d,0x40,0x21,0x35,0xd5,0x40,0x13,0x03,0xe9,0x80};
uint16_t L4S_Read16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
uint32_t L4S_Read32(const uint8_t *p) { return L4S_Read16(p) | (uint32_t)L4S_Read16(p + 2) << 16; }
void L4S_Write16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
void L4S_Write32(uint8_t *p, uint32_t n) { L4S_Write16(p,n); L4S_Write16(p+2,n>>16); }

uint16_t L4S_Crc(const uint8_t *p, size_t n)
{
    uint16_t crc = 0;
    while (n--) {
        crc ^= (uint16_t)*p++ << 8;
        for (unsigned j = 0; j < 8; ++j) crc = (crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0);
    }
    return crc;
}

size_t L4S_EncodeFrame(const uint8_t *body, size_t n, uint8_t *frame)
{
    if (n > L4S_MAX_BODY) return 0;
    frame[0] = 0xab; frame[1] = 0xcd; L4S_Write16(frame + 2, n);
    memcpy(frame + 4, body, n);
    L4S_Write16(frame + 4 + n, L4S_Crc(body, n));
    for (size_t i = 0; i < n + 2; ++i) frame[4 + i] ^= mask[i % 16];
    frame[n + 6] = 0xdc; frame[n + 7] = 0xba;
    return n + 8;
}

void L4S_Init(L4S_State *s, const L4S_Ops *ops)
{
    memset(s, 0, sizeof(*s)); s->ops = ops;
}

static void stop(L4S_State *s, uint8_t error)
{
    s->ops->stop();
    if (s->owner == L4S_TX_CW) s->cw_state = L4S_CW_CANCELLED;
    s->owner = L4S_TX_OFF;
    s->tx_window_open = false;
    s->pair_ready = false; /* Any subsequent TX needs a newly applied frequency revision. */
    s->error = error;
}

void L4S_Abort(L4S_State *s, uint8_t reason)
{
    if (!s->session) return;
    stop(s, reason);
    s->retired_session = s->session;
    s->session = 0;
    s->pair_ready = s->mode_ready = false;
    s->ops->leave();
}

void L4S_StopTx(L4S_State *s, uint8_t reason) { stop(s,reason); }

uint8_t L4S_LocalPtt(L4S_State *s, bool pressed, uint32_t now)
{
    if (!s->session) return L4S_BAD_SESSION;
    if (!pressed) {
        if (s->owner==L4S_TX_LOCAL) stop(s,0);
        return L4S_APPLIED;
    }
    if (s->owner==L4S_TX_LOCAL) return L4S_APPLIED;
    /* A physical press cancels an existing remote operation, but never rekeys on the same press. */
    if (s->tx_window_open || s->owner || s->ops->tx_state()) {
        stop(s,L4S_CANCELLED); return L4S_CANCELLED;
    }
    if (!s->pair_ready || !s->mode_ready || !s->applied.tx_enabled) return s->error=L4S_NOT_READY;
    if (!s->ops->ptt_on()) return s->error=L4S_TX_REJECTED;
    s->owner=L4S_TX_LOCAL; s->tx_window_open=true; s->tx_started=now; s->error=0;
    s->cw_state=L4S_CW_IDLE; s->cw_length=0;
    return L4S_APPLIED;
}

void L4S_Tick(L4S_State *s, uint32_t now)
{
    if (!s->session) return;
    if ((uint32_t)(now - s->last_alive) >= L4S_TIMEOUT_MS) { L4S_Abort(s, L4S_TIMEOUT); return; }
    if (s->owner == L4S_TX_CW) {
        if (s->ops->cw_failed()) {
            stop(s, L4S_TX_REJECTED); s->cw_state = L4S_CW_FAILED;
        } else if (!s->ops->cw_active()) {
            s->ops->stop(); s->owner = L4S_TX_OFF; s->cw_state = L4S_CW_DONE;
        } else s->cw_state = L4S_CW_SENDING;
    } else if ((s->owner == L4S_TX_VOICE || s->owner == L4S_TX_LOCAL) && s->ops->tx_state() == L4S_TX_OFF) {
        stop(s, L4S_TX_REJECTED);
    }
}

static size_t snapshot(L4S_State *s, uint8_t *p)
{
    L4S_Write32(p, s->applied.rx_hz); L4S_Write32(p+4, s->applied.tx_hz);
    L4S_Write32(p+8, s->applied.revision);
    L4S_Write32(p+12, 0); /* Reserved; frequency changes are never queued during TX. */
    p[16] = s->rx_mode; p[17] = s->tx_mode; p[18] = s->applied.tx_enabled;
    p[19] = s->ops->tx_state();
    if (s->owner==L4S_TX_LOCAL) p[19]=L4S_TX_LOCAL;
    if (!p[19] && s->tx_window_open) p[19]=L4S_TX_CW;
    p[20] = s->cw_state;
    p[21] = s->cw_state == L4S_CW_DONE ? s->cw_length : s->ops->cw_progress();
    p[22] = s->cw_length; p[23] = s->error;
    return L4S_SNAPSHOT_SIZE;
}

static uint8_t execute(L4S_State *s, uint16_t cmd, const uint8_t *p, size_t n)
{
    switch (cmd) {
    case L4S_KEEPALIVE: case L4S_STATUS:
        return n == 0 ? L4S_APPLIED : L4S_BAD_LENGTH;
    case L4S_STOP:
        if (n) return L4S_BAD_LENGTH;
        stop(s, 0); return L4S_APPLIED;
    case L4S_CLOSE:
        if (n) return L4S_BAD_LENGTH;
        L4S_Abort(s, 0); return L4S_APPLIED;
    case L4S_PAIR: {
        if (n != 13 || p[8] > 1) return L4S_BAD_LENGTH;
        /* Hold BOTH legs throughout voice TX and the complete CW message, including gaps. */
        if (s->tx_window_open || s->owner || s->ops->tx_state() || !s->ops->can_retune()) return L4S_BUSY;
        L4S_Pair pair = {L4S_Read32(p), L4S_Read32(p+4), L4S_Read32(p+9), p[8] != 0};
        if (pair.rx_hz > UINT32_MAX - 5 || pair.tx_hz > UINT32_MAX - 5) return L4S_RANGE;
        pair.rx_hz = (pair.rx_hz + 5) / 10 * 10;
        pair.tx_hz = pair.tx_enabled ? (pair.tx_hz + 5) / 10 * 10 : 0;
        uint32_t revision = s->applied.revision;
        if ((int32_t)(pair.revision - revision) <= 0) return L4S_STALE;
        if (!s->ops->validate_pair(&pair)) return L4S_RANGE;
        if (!s->ops->apply_pair(&pair)) return L4S_TX_REJECTED;
        s->applied = pair; s->pair_ready = true;
        s->error=0;
        return L4S_APPLIED;
    }
    case L4S_MODE:
        if (n != 3) return L4S_BAD_LENGTH;
        if (s->tx_window_open || s->owner || s->ops->tx_state()) return L4S_BUSY;
        if (p[0] > L4S_CW_MODE || (p[1] != L4S_FM && p[1] != L4S_CW_MODE) || p[2] > 2) return L4S_UNSUPPORTED;
        if (!s->ops->mode(p[0],p[1],p[2])) return L4S_TX_REJECTED;
        s->rx_mode=p[0]; s->tx_mode=p[1]; s->narrow=p[2]; s->mode_ready=true;
        return L4S_APPLIED;
    case L4S_TONE:
        if (n != 2) return L4S_BAD_LENGTH;
        if (s->tx_window_open || s->owner || s->ops->tx_state()) return L4S_BUSY;
        return s->ops->tone(L4S_Read16(p)) ? L4S_APPLIED : L4S_RANGE;
    case L4S_PTT:
        if (n != 5 || p[0] > 1) return L4S_BAD_LENGTH;
        if (!p[0]) { stop(s, 0); return L4S_APPLIED; }
        if (s->tx_window_open || s->owner || s->ops->tx_state()) return L4S_BUSY;
        if (!s->pair_ready || !s->mode_ready || !s->applied.tx_enabled ||
            L4S_Read32(p+1) != s->applied.revision) return L4S_NOT_READY;
        if (s->tx_mode != L4S_FM) return L4S_UNSUPPORTED;
        if (!s->ops->ptt_on()) return L4S_TX_REJECTED;
        s->owner = L4S_TX_VOICE; return L4S_APPLIED;
    case L4S_CW:
        if (n < 3 || p[1] == 0 || p[1] > L4S_MAX_TEXT || n != (size_t)p[1] + 2) return L4S_BAD_LENGTH;
        if (s->owner || s->ops->tx_state()) return L4S_BUSY;
        if (!s->pair_ready || !s->mode_ready || !s->applied.tx_enabled) return L4S_NOT_READY;
        if (s->tx_mode != L4S_CW_MODE) return L4S_UNSUPPORTED;
        if (p[0] < 10 || p[0] > 45) return L4S_RANGE;
        if (!s->ops->cw_start(p+2,p[1],p[0])) return L4S_TX_REJECTED;
        s->owner=L4S_TX_CW; s->cw_state=L4S_CW_QUEUED; s->cw_length=p[1];
        return L4S_ACCEPTED;
    default: return L4S_UNSUPPORTED;
    }
}

size_t L4S_Handle(L4S_State *s, const uint8_t *body, size_t length, uint32_t now, uint8_t *out)
{
    if (length < 12 || length > L4S_MAX_BODY || L4S_Read16(body+2) != length - 4) return 0;
    const uint16_t cmd=L4S_Read16(body), seq=L4S_Read16(body+6);
    const uint32_t session=L4S_Read32(body+8);
    if (cmd < L4S_HELLO || cmd > L4S_STOP) return 0;
    L4S_Tick(s, now);
    uint8_t result=L4S_APPLIED;
    if (body[4] != L4S_VERSION || body[5] != 0) result=L4S_UNSUPPORTED;
    else if (cmd == L4S_HELLO) { if (length != 12) result=L4S_BAD_LENGTH; }
    else if (cmd == L4S_OPEN) {
        if (length != 12 || session == 0) result=L4S_BAD_LENGTH;
        else if (s->session) result=s->session == session ? L4S_APPLIED : L4S_BUSY;
        else if (s->retired_session == session) result=L4S_BAD_SESSION;
        else if (!s->ops->enter()) result=L4S_BUSY;
        else {
            s->session=session; s->have_seq=false; s->applied.revision=0;
            s->pair_ready=s->mode_ready=false; s->cw_state=L4S_CW_IDLE;
            s->error=0; s->last_alive=now;
        }
    } else if (!s->session || session != s->session) result=L4S_BAD_SESSION;
    else {
        const uint16_t hash=L4S_Crc(body,length);
        if (cmd == L4S_STOP) result=execute(s,cmd,body+12,length-12);
        else if (s->have_seq && seq == s->last_seq && cmd == s->last_cmd && hash == s->last_hash)
            result=s->last_result;
        else if (s->have_seq && (int16_t)(seq-s->last_seq) <= 0) result=L4S_STALE;
        else {
            result=execute(s,cmd,body+12,length-12);
            if (result<=L4S_ACCEPTED && (cmd==L4S_CW || (cmd==L4S_PTT && body[12]==1)) &&
                !s->tx_window_open) { s->tx_window_open=true; s->tx_started=now; }
            if (result <= L4S_ACCEPTED) s->last_alive=now;
        }
        if (!s->have_seq || (int16_t)(seq-s->last_seq) >= 0) {
            s->last_seq=seq; s->last_cmd=cmd; s->last_hash=hash; s->last_result=result; s->have_seq=true;
        }
    }
    L4S_Write16(out,cmd | 0x8000); out[4]=L4S_VERSION; out[5]=result;
    L4S_Write16(out+6,seq); L4S_Write32(out+8,s->session);
    size_t data_size;
    if (cmd == L4S_HELLO && result == L4S_APPLIED) {
        uint8_t *p=out+12;
        memcpy(p,"L4S1",4); p[4]=3; /* UV-K1 / UV-K5 V3 shared firmware */
        uint32_t features=L4S_CAP_PAIR|L4S_CAP_PTT|L4S_CAP_CW|L4S_CAP_TONE;
        L4S_Write32(p+5,features);
        L4S_Write16(p+9,10); p[11]=L4S_MAX_TEXT; p[12]=10; p[13]=45;
        L4S_Write16(p+14,L4S_TIMEOUT_MS);
        L4S_Write32(p+16,18000000u); L4S_Write32(p+20,1300000000u);
        memset(p+24,0,16);
#ifdef VERSION_STRING_2
        strncpy((char *)p+24,VERSION_STRING_2,15);
#else
        memcpy(p+24,"L4S host-v1",11);
#endif
        p[40]=0x0f; p[41]=0x09; /* RX FM/AM/USB/CW; TX FM/CW only. */
        data_size=42;
    } else data_size=snapshot(s,out+12);
    L4S_Write16(out+2,L4S_META_SIZE+data_size);
    return 12+data_size;
}
