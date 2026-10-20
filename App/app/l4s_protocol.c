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
    return L4S_CrcContinue(0,p,n);
}
uint16_t L4S_CrcContinue(uint16_t crc,const uint8_t *p,size_t n)
{
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
    L4S_DefaultConfig(&s->settings);
    s->step_hz=250;
    s->correction_step_hz=10;
}

static void stop(L4S_State *s, uint8_t error)
{
    s->ops->stop();
    if (s->owner == L4S_TX_CW) s->cw_state = L4S_CW_CANCELLED;
    s->owner = L4S_TX_OFF;
    s->tx_window_open = false;
    s->arm_next=false;
    s->arm_active=false;
    s->pair_ready = false; /* Any subsequent TX needs a newly applied frequency revision. */
    s->error = error;
    s->stop_reason=error;s->local_macro=false;
}

void L4S_Abort(L4S_State *s, uint8_t reason)
{
    if (!s->session) return;
    s->aborts++;
    stop(s, reason);
    s->retired_session = s->session;
    s->session = 0;
    s->pair_ready = s->mode_ready = false;
    s->ops->leave();
}

void L4S_StopTx(L4S_State *s, uint8_t reason) { stop(s,reason); }

uint8_t L4S_LocalPtt(L4S_State *s, bool pressed, uint32_t now)
{
    L4S_Tick(s,now);
    if (!s->session) return L4S_BAD_SESSION;
    if (!pressed) {
        if (s->owner==L4S_TX_LOCAL) stop(s,0);
        return L4S_APPLIED;
    }
    if (s->menu_open || s->store_busy) return L4S_BUSY;
    if (s->owner==L4S_TX_LOCAL) return L4S_APPLIED;
    /* A physical press cancels an existing remote operation, but never rekeys on the same press. */
    if (s->tx_window_open || s->owner || s->ops->tx_state()) {
        stop(s,L4S_CANCELLED); return L4S_CANCELLED;
    }
    if (!s->pair_ready || !s->mode_ready || !s->applied.tx_enabled) return s->error=L4S_NOT_READY;
    s->arm_active=s->arm_next && s->tx_mode==L4S_FM && s->settings.arm_tone;
    s->arm_next=false;
    if (!s->ops->ptt_on()) {s->arm_active=false;return s->error=L4S_TX_REJECTED;}
    s->owner=L4S_TX_LOCAL; s->tx_window_open=true; s->tx_started=now; s->error=0;
    s->cw_state=L4S_CW_IDLE; s->cw_length=0;
    return L4S_APPLIED;
}

void L4S_Tick(L4S_State *s, uint32_t now)
{
    if (!s->session) return;
    if(s->pass_phase && (uint32_t)(now-s->pass_updated)>=1000){
        uint32_t seconds=(now-s->pass_updated)/1000;s->pass_updated+=seconds*1000;
        s->pass_seconds=seconds>s->pass_seconds?0:s->pass_seconds-seconds;
    }
    if ((uint32_t)(now - s->last_alive) >= L4S_TIMEOUT_MS) { L4S_Abort(s, L4S_TIMEOUT); return; }
    if (s->owner == L4S_TX_CW) {
        if (s->ops->cw_failed()) {
            stop(s, L4S_TX_REJECTED); s->cw_state = L4S_CW_FAILED;
        } else if (!s->ops->cw_active()) {
            s->ops->stop(); s->owner = L4S_TX_OFF; s->cw_state = L4S_CW_DONE;
            if(s->local_macro)stop(s,0);
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
    case L4S_DIAGNOSTICS:return n==0?L4S_APPLIED:L4S_BAD_LENGTH;
    case L4S_PASS_INFO:
        if(n!=5)return L4S_BAD_LENGTH;
        if(p[0]>3||L4S_Read32(p+1)>604800u)return L4S_RANGE;
        s->pass_phase=p[0];s->pass_seconds=L4S_Read32(p+1);return L4S_APPLIED;
    case L4S_CW_OPTIONS:{
        if(n==1&&p[0]==0)return L4S_APPLIED;
        if(n!=7||p[0]!=1)return L4S_BAD_LENGTH;
        if(s->menu_open||s->store_busy||s->tx_window_open||s->owner||s->ops->tx_state()||!s->ops->can_retune())return L4S_BUSY;
        if(!s->profile_selected||!s->ops->apply_settings)return L4S_NOT_READY;
        L4S_UserConfig c=s->settings;c.wpm=p[1];c.pitch=p[2];c.volume=p[3];c.cw_hang=p[4];c.cw_keyer=p[5];c.cw_spacing=p[6];
        if(!c.cw_hang||!L4S_ValidConfig(&c))return L4S_RANGE;
        if(!s->ops->apply_settings(&c))return L4S_BUSY;
        s->settings=c;s->user_modified=true;s->profile_generation++;s->pair_ready=false;return L4S_APPLIED;
    }
    case L4S_MACROS:return s->ops->macros?s->ops->macros(p,n):L4S_UNSUPPORTED;
    case L4S_PROFILE_STORE: {
        uint8_t result=s->ops->store ? s->ops->store(p,n) : L4S_UNSUPPORTED;
        if(result==L4S_APPLIED && n && p[0]==7)s->pair_ready=false;
        return result;
    }
    case L4S_PROFILE: {
        if(!n || (p[0]==0 && n!=49) || (p[0]==1 && n!=15) || (p[0]==2 && n!=1)) return L4S_BAD_LENGTH;
        if(p[0]>2) return L4S_UNSUPPORTED;
        if(p[0]==2) return L4S_APPLIED;
        if(s->menu_open || s->store_busy || s->tx_window_open || s->owner || s->ops->tx_state() || !s->ops->can_retune()) return L4S_BUSY;
        if(p[0]==0) {
            for(unsigned i=33;i<49;i++) if(p[i] && (p[i]<' ' || p[i]>'~')) return L4S_RANGE;
            if(!s->ops->profile) return L4S_UNSUPPORTED;
            return s->ops->profile(0,p+1);
        }
        L4S_UserConfig candidate=s->settings;
        if(L4S_Read32(p+11)!=s->profile_generation) return L4S_STALE;
        candidate.rx_base_hz=L4S_Read32(p+1);candidate.tx_base_hz=L4S_Read32(p+5);
        uint16_t step=L4S_Read16(p+9);
        if(!candidate.rx_base_hz || !L4S_ValidConfig(&candidate) || step<10 || step>10000 || step%10) return L4S_RANGE;
        s->settings=candidate;s->step_hz=step;s->pair_ready=false;
        return L4S_APPLIED;
    }
    case L4S_CONTEXT: {
        if (n!=20 && n!=36) return L4S_BAD_LENGTH;
        const uint16_t az=L4S_Read16(p);
        const int16_t el=(int16_t)L4S_Read16(p+2);
        if(n==36 && s->profile_selected && memcmp(s->context_key,p+20,16)) return L4S_STALE;
        if(n==36 && az==0xffff && el==INT16_MIN) {
            memcpy(s->context_key,p+20,16);s->context_valid=false;return L4S_APPLIED;
        }
        if (az>=3600 || el < -900 || el>900) return L4S_RANGE;
        for (unsigned i=4;i<20;i++) if (p[i] && (p[i]<' ' || p[i]>'~')) return L4S_RANGE;
        memcpy(s->satellite_name,p+4,16); s->satellite_name[16]=0;
        s->azimuth_tenths=az; s->elevation_tenths=el; s->context_valid=true;
        if(n==36) memcpy(s->context_key,p+20,16);
        return L4S_APPLIED; /* Display-only; does not tune or change TX ownership. */
    }
    case L4S_KEEPALIVE: case L4S_STATUS: case L4S_SETTINGS:
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
        if (s->store_busy || s->tx_window_open || s->owner || s->ops->tx_state() || !s->ops->can_retune()) return L4S_BUSY;
        L4S_Pair pair = {L4S_Read32(p), L4S_Read32(p+4), L4S_Read32(p+9), p[8] != 0};
        if (pair.rx_hz > UINT32_MAX - 5 || pair.tx_hz > UINT32_MAX - 5) return L4S_RANGE;
        pair.rx_hz = (pair.rx_hz + 5) / 10 * 10;
        pair.tx_hz = pair.tx_enabled ? (pair.tx_hz + 5) / 10 * 10 : 0;
        uint32_t revision = s->applied.revision;
        if ((int32_t)(pair.revision - revision) <= 0) return L4S_STALE;
        if (!s->ops->validate_pair(&pair)) return L4S_RANGE;
        const bool unchanged=s->pair_ready && pair.rx_hz==s->applied.rx_hz &&
            pair.tx_hz==s->applied.tx_hz && pair.tx_enabled==s->applied.tx_enabled;
        if (!unchanged && !s->ops->apply_pair(&pair)) return L4S_TX_REJECTED;
        s->applied = pair; s->pair_ready = true;
        s->error=0;
        return L4S_APPLIED;
    }
    case L4S_MODE:
        if (n != 3) return L4S_BAD_LENGTH;
        if (s->store_busy || s->tx_window_open || s->owner || s->ops->tx_state()) return L4S_BUSY;
        if (p[0] > L4S_CW_MODE || (p[1] != L4S_FM && p[1] != L4S_CW_MODE) || p[2] > 2) return L4S_UNSUPPORTED;
        if (!s->ops->mode(p[0],p[1],p[2])) return L4S_TX_REJECTED;
        s->rx_mode=p[0]; s->tx_mode=p[1]; s->narrow=p[2]; s->mode_ready=true;
        s->settings.mode=p[1]==L4S_FM ? (p[2] ? 3 : 0) : p[0]==L4S_USB ? 2 : 1;
        s->user_modified=false;
        return L4S_APPLIED;
    case L4S_TONE:
        if (n != 2) return L4S_BAD_LENGTH;
        if (s->store_busy || s->tx_window_open || s->owner || s->ops->tx_state()) return L4S_BUSY;
        if (!s->ops->tone(L4S_Read16(p))) return L4S_RANGE;
        if (s->tx_mode==L4S_FM) s->settings.tx_tone=L4S_CtcssChoice(L4S_Read16(p));
        return L4S_APPLIED;
    case L4S_PTT:
        if (n != 5 || p[0] > 1) return L4S_BAD_LENGTH;
        if (!p[0]) { stop(s, 0); return L4S_APPLIED; }
        if (s->menu_open || s->store_busy) return L4S_BUSY;
        if (s->tx_window_open || s->owner || s->ops->tx_state()) return L4S_BUSY;
        if (!s->pair_ready || !s->mode_ready || !s->applied.tx_enabled ||
            L4S_Read32(p+1) != s->applied.revision) return L4S_NOT_READY;
        if (s->tx_mode != L4S_FM) return L4S_UNSUPPORTED;
        if (!s->ops->ptt_on()) return L4S_TX_REJECTED;
        s->owner = L4S_TX_VOICE; return L4S_APPLIED;
    case L4S_CW:
        if (n < 3 || p[1] == 0 || p[1] > L4S_MAX_TEXT || n != (size_t)p[1] + 2) return L4S_BAD_LENGTH;
        if (s->menu_open || s->store_busy) return L4S_BUSY;
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

uint8_t L4S_LocalCw(L4S_State *s,const uint8_t *text,uint8_t length,uint32_t now)
{
    L4S_Tick(s,now);if(!s->session)return L4S_BAD_SESSION;
    if(!length||length>L4S_MAX_TEXT)return L4S_RANGE;
    uint8_t p[L4S_MAX_TEXT+2];p[0]=s->settings.wpm;p[1]=length;memcpy(p+2,text,length);
    uint8_t result=execute(s,L4S_CW,p,length+2);
    if(result==L4S_ACCEPTED){s->tx_window_open=true;s->local_macro=true;s->tx_started=now;s->error=0;}
    else s->error=result;
    return result;
}
size_t L4S_Handle(L4S_State *s, const uint8_t *body, size_t length, uint32_t now, uint8_t *out)
{
    if (length < 12 || length > L4S_MAX_BODY || L4S_Read16(body+2) != length - 4) {s->length_errors++;return 0;}
    const uint16_t cmd=L4S_Read16(body), seq=L4S_Read16(body+6);
    const uint32_t session=L4S_Read32(body+8);
    if (cmd < L4S_HELLO || cmd > L4S_PASS_INFO) return 0;
    s->requests++;
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
            s->context_valid=false;s->pass_phase=0;
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
            if (result <= L4S_ACCEPTED) {
                uint32_t gap=now-s->last_alive;if(gap>s->max_gap)s->max_gap=gap;
                s->last_alive=now;if(cmd==L4S_PASS_INFO)s->pass_updated=now;
            }
        }
        if (!s->have_seq || (int16_t)(seq-s->last_seq) >= 0) {
            s->last_seq=seq; s->last_cmd=cmd; s->last_hash=hash; s->last_result=result; s->have_seq=true;
        }
    }
    if(result>L4S_ACCEPTED){s->rejected++;s->last_rejection=result;}
    L4S_Write16(out,cmd | 0x8000); out[4]=L4S_VERSION; out[5]=result;
    L4S_Write16(out+6,seq); L4S_Write32(out+8,s->session);
    size_t data_size;
    if (cmd == L4S_HELLO && result == L4S_APPLIED) {
        uint8_t *p=out+12;
        memcpy(p,"L4S1",4); p[4]=3; /* UV-K1 / UV-K5 V3 shared firmware */
        uint32_t features=L4S_CAP_PAIR|L4S_CAP_PTT|L4S_CAP_CW|L4S_CAP_TONE|L4S_CAP_CONTEXT|L4S_CAP_SETTINGS|L4S_CAP_PROFILES|L4S_CAP_STORE;
        features|=L4S_CAP_DIAGNOSTICS|L4S_CAP_PASS_INFO;
        if(s->ops->apply_settings)features|=L4S_CAP_CW_OPTIONS;
        if(s->ops->macros)features|=L4S_CAP_MACROS;
        L4S_Write32(p+5,features);
        L4S_Write16(p+9,10); p[11]=L4S_MAX_TEXT; p[12]=10; p[13]=45;
        L4S_Write16(p+14,L4S_TIMEOUT_MS);
        L4S_Write32(p+16,18000000u); L4S_Write32(p+20,1300000000u);
        memset(p+24,0,16);
#ifdef VERSION_STRING_2
        const size_t version_length=sizeof(VERSION_STRING_2)-1;
        memcpy(p+24,VERSION_STRING_2,version_length>15 ? 15 : version_length);
#else
        memcpy(p+24,"L4S host-v1",11);
#endif
        p[40]=0x0f; p[41]=0x09; /* RX FM/AM/USB/CW; TX FM/CW only. */
        data_size=42;
    } else if(cmd==L4S_DIAGNOSTICS && result==L4S_APPLIED) {
        uint8_t *p=out+12;p[0]=1;p[1]=s->stop_reason;p[2]=s->last_rejection;p[3]=s->rssi;
        uint32_t counters[]={s->requests,s->rejected,s->aborts,s->usb_losses,s->reply_drops,s->max_gap,s->crc_errors,s->length_errors};
        for(unsigned i=0;i<8;i++)L4S_Write32(p+4+i*4,counters[i]);
        data_size=36;
    } else if(cmd==L4S_CW_OPTIONS && result==L4S_APPLIED) {
        uint8_t *p=out+12;p[0]=1;p[1]=s->settings.wpm;p[2]=s->settings.pitch;p[3]=s->settings.volume;
        p[4]=s->settings.cw_hang?s->settings.cw_hang:30;p[5]=s->settings.cw_hang?s->settings.cw_keyer:1;
        p[6]=s->settings.cw_spacing;p[7]=s->user_modified;data_size=8;
    } else if(cmd==L4S_MACROS && result<=L4S_ACCEPTED && s->ops->macros_reply) {
        data_size=s->ops->macros_reply(body+12,length-12,out+12);
    } else if(cmd==L4S_PROFILE_STORE && result<=L4S_ACCEPTED && s->ops->store_reply) {
        data_size=s->ops->store_reply(body+12,out+12);
    } else {
        data_size=snapshot(s,out+12);
        if ((cmd==L4S_SETTINGS || cmd==L4S_PROFILE) && result==L4S_APPLIED) {
            uint8_t *p=out+12+data_size;
            memcpy(p,s->saved_settings_valid ? &s->saved_settings : &s->settings,8);
            L4S_Write16(p+8,s->tx_mode==L4S_FM ? L4S_CtcssValue(s->arm_active ? s->settings.arm_tone : s->settings.tx_tone) : 0);
            L4S_Write16(p+10,s->rx_mode==L4S_FM ? L4S_CtcssValue(s->settings.rx_tone) : 0);
            L4S_Write16(p+12,L4S_CtcssValue(s->settings.arm_tone));
            p[14]=(s->menu_open || s->store_busy ? 1 : 0)|(s->arm_next ? 2 : 0)|(s->arm_active ? 4 : 0)|
                (s->user_modified ? 8 : 0)|(s->saved_settings_valid ? 16 : 0);
            memcpy(p+15,s->saved_key,16);
            data_size+=31;
            if(cmd==L4S_PROFILE) {
                p=out+12+data_size;
                p[0]=s->settings.squelch;
                L4S_Write32(p+1,s->settings.rx_base_hz);L4S_Write32(p+5,s->settings.tx_base_hz);
                L4S_Write32(p+9,(uint32_t)s->settings.rx_correction_hz);L4S_Write32(p+13,(uint32_t)s->settings.tx_correction_hz);
                L4S_Write16(p+17,s->step_hz);L4S_Write32(p+19,s->profile_generation);
                p[23]=L4S_SAT_PROFILE_COUNT;data_size+=24;
            }
        }
    }
    L4S_Write16(out+2,L4S_META_SIZE+data_size);
    return 12+data_size;
}
