/* Host-only, no hardware access. Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_protocol.h"
#include "l4s_io.h"
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static L4S_State state;
static L4S_MenuState menu;
static bool key_latch;
const L4S_MenuState *L4S_GetMenu(void) { return &menu; }
const L4S_State *L4S_GetState(void) { return &state; }
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
void Host_StorageRead(uint32_t,void *,unsigned);
void Host_StorageWrite(uint32_t,const void *,unsigned);
static uint8_t profile(uint8_t operation,const uint8_t *p) {
    (void)operation;
    L4S_DefaultConfig(&state.settings);
    state.saved_settings_valid=L4S_LoadSatelliteProfile(p,&state.settings,Host_StorageRead);
    state.saved_settings=state.settings;memcpy(state.context_key,p,16);memcpy(state.saved_key,p,16);
    memcpy(state.satellite_name,p+32,16);state.satellite_name[16]=0;
    state.profile_selected=true;state.profile_generation++;
    state.pair_ready=state.mode_ready=false;state.context_valid=false;state.user_modified=false;
    return 0;
}
static const L4S_Ops ops={enter,leave,validate,apply,mode,tone,ptt,stop,cw,
                         tx_state,cw_active,cw_failed,cw_progress,can_retune,profile};
EXPORT void Host_Reset(void) { tx=progress=0;active=denied=false;starts=leaves=writes=0;clock_ms=0;key_latch=false;memset(&menu,0,sizeof(menu));L4S_Init(&state,&ops); }
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
EXPORT uint8_t Host_LocalPtt(uint8_t pressed,uint32_t now) { return L4S_LocalPtt(&state,pressed!=0,now); }
EXPORT bool Host_Key(unsigned key,bool pressed) { return L4S_FilterKey(&state,(KEY_Code_t)key,pressed,&key_latch); }
EXPORT void Host_MenuOpen(void) { menu=(L4S_MenuState){.draft=state.settings,.open=true,.step_hz=state.step_hz};state.menu_open=true; }
EXPORT bool Host_MenuKey(unsigned key) {
    bool apply=L4S_MenuKey(&menu,(KEY_Code_t)key,state.tx_window_open);
    state.menu_open=menu.open;return apply;
}
EXPORT void Host_MenuDraft(L4S_UserConfig *config) {*config=menu.draft;}
EXPORT void Host_ArmNext(uint8_t choice) { state.settings.arm_tone=choice;state.arm_next=true; }
EXPORT size_t Host_Encode(const uint8_t *body,size_t n,uint8_t *out) { return L4S_EncodeFrame(body,n,out); }
