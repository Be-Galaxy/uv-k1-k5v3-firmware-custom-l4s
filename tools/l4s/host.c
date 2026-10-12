/* Host-only, no hardware access. Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_protocol.h"
#include "l4s_io.h"
#include "app/l4s_control.h"
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
void Host_StoreCancel(void);
static uint32_t observed_store_generation;
static bool enter(void) { return !tx && !L4S_StoreBusy(L4S_GetStore()); }
static void leave(void) { Host_StoreCancel();leaves++; }
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
void Host_StoreTick(uint32_t);
uint8_t Host_StoreCommand(const uint8_t *,unsigned,bool,uint16_t *);
size_t Host_StoreReply(const uint8_t *,uint16_t,uint8_t *);
void Host_StorageRead(uint32_t,void *,unsigned);
void Host_StorageWrite(uint32_t,const void *,unsigned);
static void profile_read(uint32_t address,void *out,unsigned n) {
    if(address>=L4S_SAT_PROFILE_ADDR && address<L4S_SAT_PROFILE_ADDR+56*64 && n==64)
        L4S_StoreRead(L4S_GetStore(),(address-L4S_SAT_PROFILE_ADDR)/64,out);
    else Host_StorageRead(address,out,n);
}
static uint8_t profile(uint8_t operation,const uint8_t *p) {
    (void)operation;
    L4S_DefaultConfig(&state.settings);
    state.saved_settings_valid=L4S_LoadSatelliteProfile(p,&state.settings,profile_read);
    state.saved_settings=state.settings;memcpy(state.context_key,p,16);memcpy(state.saved_key,p,16);
    memcpy(state.satellite_name,p+32,16);state.satellite_name[16]=0;
    state.profile_selected=true;state.profile_generation++;
    state.pair_ready=state.mode_ready=false;state.context_valid=false;state.user_modified=false;
    return 0;
}
static uint8_t store_command(const uint8_t *p,size_t n) {
    uint8_t result=Host_StoreCommand(p,n,!tx && !active && !menu.open && !state.tx_window_open,&state.correction_step_hz);
    state.store_busy=L4S_StoreBusy(L4S_GetStore());if(result==1)state.pair_ready=false;return result;
}
static size_t store_reply(const uint8_t *p,uint8_t *out) {
    size_t n=Host_StoreReply(p,state.correction_step_hz,out);
    if(!state.saved_settings_valid || !L4S_ConfigEqual(&state.settings,&state.saved_settings))out[4]|=8;
    return n;
}
static const L4S_Ops ops={enter,leave,validate,apply,mode,tone,ptt,stop,cw,
                         tx_state,cw_active,cw_failed,cw_progress,can_retune,profile,store_command,store_reply};
void Host_StoreEnsure(void);
EXPORT void Host_Reset(void) { Host_StoreEnsure();tx=progress=0;active=denied=false;starts=leaves=writes=0;clock_ms=0;key_latch=false;memset(&menu,0,sizeof(menu));L4S_Init(&state,&ops);observed_store_generation=L4S_GetStore()->generation; }
EXPORT void Host_Tick(uint32_t now) {
    clock_ms=now;
    L4S_Tick(&state,now);
    Host_StoreTick(now);state.store_busy=L4S_StoreBusy(L4S_GetStore());
    if(L4S_GetStore()->generation!=observed_store_generation) {
        observed_store_generation=L4S_GetStore()->generation;state.pair_ready=false;
        if(state.profile_selected) {
            L4S_UserConfig current;L4S_DefaultConfig(&current);
            bool found=L4S_LoadSatelliteProfile(state.context_key,&current,profile_read);
            if(found!=state.saved_settings_valid || (found && !L4S_ConfigEqual(&current,&state.saved_settings))) {
                state.settings=state.saved_settings=current;state.saved_settings_valid=found;state.user_modified=true;state.profile_generation++;
                if(!found){state.applied.tx_enabled=false;state.mode_ready=false;}
            }
        }
    }
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
EXPORT void Host_MenuOpen(void) { menu=(L4S_MenuState){.draft=state.settings,.open=true,.step_hz=state.step_hz,.correction_step_hz=state.correction_step_hz};state.menu_open=true; }
EXPORT bool Host_MenuKey(unsigned key) {
    bool apply=L4S_MenuKey(&menu,(KEY_Code_t)key,state.tx_window_open);
    state.menu_open=menu.open;return apply;
}
EXPORT void Host_MenuDraft(L4S_UserConfig *config) {*config=menu.draft;}
EXPORT void Host_ArmNext(uint8_t choice) { state.settings.arm_tone=choice;state.arm_next=true; }
EXPORT size_t Host_Encode(const uint8_t *body,size_t n,uint8_t *out) { return L4S_EncodeFrame(body,n,out); }
