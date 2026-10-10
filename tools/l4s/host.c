/* Host-only, no hardware access. Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_protocol.h"
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static L4S_State state;
static uint8_t tx, progress;
static bool active, denied;
static uint32_t clock_ms, cw_start_ms;
static unsigned starts, leaves, writes;
static bool enter(void) { return !tx; }
static void leave(void) { leaves++; }
static bool validate(const L4S_Pair *p) {
    return p->rx_hz>=18000000u && p->rx_hz<=1300000000u &&
        (!p->tx_enabled || (p->tx_hz>=144000000u && p->tx_hz<=148000000u) ||
         (p->tx_hz>=430000000u && p->tx_hz<=450000000u));
}
static bool apply(const L4S_Pair *p) { (void)p; writes++; return true; }
static bool mode(uint8_t rx,uint8_t tx_mode,uint8_t narrow) { (void)rx;(void)tx_mode;(void)narrow; return true; }
static bool tone(uint16_t value) { return value==0 || value==670 || value==885; }
static bool ptt(void) { if(denied)return false; tx=L4S_TX_VOICE; starts++; return true; }
static void stop(void) { tx=0; active=false; }
static bool cw(const uint8_t *text,uint8_t n,uint8_t wpm) {
    (void)wpm;
    if(denied)return false;
    for(unsigned i=0;i<n;i++) if(!strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /?.,=-+(&",text[i]))return false;
    active=true; tx=L4S_TX_CW; cw_start_ms=clock_ms; starts++; return true;
}
static uint8_t tx_state(void) { return tx; }
static bool cw_active(void) { return active; }
static bool cw_failed(void) { return denied; }
static uint8_t cw_progress(void) { return progress; }
static bool can_retune(void) { return !tx && !active; }
static const L4S_Ops ops={enter,leave,validate,apply,mode,tone,ptt,stop,cw,
                         tx_state,cw_active,cw_failed,cw_progress,can_retune};
EXPORT void Host_Reset(void) { tx=progress=0;active=denied=false;starts=leaves=writes=0;clock_ms=0;L4S_Init(&state,&ops); }
EXPORT void Host_Tick(uint32_t now) {
    clock_ms=now;
    /* Mock elapsed CW progress, not a timing or RF simulation. */
    if(active) {
        unsigned n=(now-cw_start_ms)/80;
        progress=n>state.cw_length ? state.cw_length : n;
        if(progress>=state.cw_length) { active=false; tx=0; }
    }
    L4S_Tick(&state,now);
}
EXPORT size_t Host_Handle(const uint8_t *body,size_t n,uint32_t now,uint8_t *out) {
    Host_Tick(now); return L4S_Handle(&state,body,n,now,out);
}
EXPORT void Host_Deny(uint8_t value) { denied=value!=0; }
EXPORT void Host_LocalStop(void) { L4S_Abort(&state,L4S_CANCELLED); }
EXPORT unsigned Host_Starts(void) { return starts; }
EXPORT unsigned Host_Writes(void) { return writes; }
EXPORT unsigned Host_Leaves(void) { return leaves; }
EXPORT size_t Host_Encode(const uint8_t *body,size_t n,uint8_t *out) { return L4S_EncodeFrame(body,n,out); }
