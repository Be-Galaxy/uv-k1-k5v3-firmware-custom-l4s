/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_control.h"
#include "l4s_protocol.h"
#include "app/app.h"
#include "app/chFrScanner.h"
#include "app/cwapp.h"
#include "app/cwkeyer.h"
#include "app/cwmacro.h"
#include "app/scanner.h"
#include "app/uart.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/millis.h"
#include "driver/vcp.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/ui.h"
#include <string.h>

static L4S_State state;
static bool initialized, release_latch;
static bool key_release_latch;
static uint32_t last_display_ms;
static bool ptt_raw, ptt_down;
static bool local_ptt_request;
static uint32_t ptt_changed_ms;
static volatile bool usb_lost;
static struct {
    VFO_Info_t vfo[2];
    uint8_t tx_vfo, cross, dual, battery_save, key_input, wpm;
    bool vox, breakin, cross_mode, monitor;
    int8_t scan;
} saved;

bool L4S_Active(void) { return state.session != 0; }
const L4S_State *L4S_GetState(void) { return &state; }
bool L4S_BlockPhysicalPtt(void) { return L4S_Active() || release_latch; }
void L4S_UsbLost(void) { usb_lost = true; }

static uint8_t tx_state(void)
{
    if (gCW_PlaybackActive || gCW_State != CW_INACTIVE) return L4S_TX_CW;
    if (gCurrentFunction == FUNCTION_TRANSMIT)
        return state.owner == L4S_TX_VOICE ? L4S_TX_VOICE : L4S_TX_LOCAL;
    return L4S_TX_OFF;
}

static void stop_tx(void)
{
    CW_StopPlayback();
    if (gCW_State != CW_INACTIVE) CW_EndTxNow();
    else if (gCurrentFunction == FUNCTION_TRANSMIT) {
        /* Emergency/remote release must not extend TX with Roger/CTCSS tails. */
        RADIO_SetupRegisters(true);
    }
    gPttIsPressed = false; gPttOnePushCounter = 0;
    gFlagEndTransmission = false; gRTTECountdown_10ms = 0;
    release_latch = GPIO_IsPttPressed();
    gUpdateDisplay = true; gUpdateStatus = true;
}

static bool enter(void)
{
    if (tx_state() || gCW_Recording || gScreenToDisplay != DISPLAY_MAIN ||
        SCANNER_IsScanning() || gCssBackgroundScan || gRequestSaveSettings || gRequestSaveVFO ||
        (gEeprom.CW_KEY_INPUT & CW_KEY_FLAG_USB_PORT) || GPIO_IsPttPressed() || release_latch ||
        gKeyReading0 != KEY_INVALID || gKeyReading1 != KEY_INVALID)
        return false;
    SETTINGS_SaveVfoIndicesFlush();
    memcpy(saved.vfo, gEeprom.VfoInfo, sizeof(saved.vfo));
    saved.tx_vfo=gEeprom.TX_VFO; saved.cross=gEeprom.CROSS_BAND_RX_TX;
    saved.dual=gEeprom.DUAL_WATCH; saved.battery_save=gEeprom.BATTERY_SAVE;
    saved.vox=gEeprom.VOX_SWITCH; saved.key_input=gEeprom.CW_KEY_INPUT;
    saved.wpm=gEeprom.CW_KEY_WPM; saved.breakin=gEeprom.CW_BREAKIN_ENABLE;
    saved.cross_mode=gCW_CrossMode; saved.monitor=gMonitor; saved.scan=gScanStateDir;
    VFO_Info_t original_rx=*gRxVfo, original_tx=*gTxVfo;
    state.applied=(L4S_Pair){original_rx.pRX->Frequency*10, original_tx.pTX->Frequency*10, 0, false};
    state.rx_mode=original_rx.Modulation; state.tx_mode=original_tx.Modulation;
    gEeprom.VfoInfo[0]=original_rx; gEeprom.VfoInfo[1]=original_tx;
    gEeprom.TX_VFO=1; gEeprom.CROSS_BAND_RX_TX=CROSS_BAND_CHAN_B;
    gEeprom.DUAL_WATCH=DUAL_WATCH_OFF; gEeprom.BATTERY_SAVE=0;
    gEeprom.VOX_SWITCH=false; gEeprom.CW_KEY_INPUT=CW_KEY_INPUT_HANDKEY;
    gEeprom.CW_BREAKIN_ENABLE=true; gCW_CrossMode=false;
    gScanStateDir=SCAN_OFF; gScheduleScanListen=gScheduleDualWatch=false;
    gVfoConfigureMode=VFO_CONFIGURE_NONE;
    gFlagReconfigureVfos=gFlagResetVfos=false;
    for (unsigned i=0;i<2;i++) {
        VFO_Info_t *v=&gEeprom.VfoInfo[i];
        v->pRX=&v->freq_config_RX; v->pTX=&v->freq_config_TX;
        v->FrequencyReverse=false; v->TX_OFFSET_FREQUENCY_DIRECTION=TX_OFFSET_FREQUENCY_DIRECTION_OFF;
        v->freq_config_RX.CodeType=CODE_TYPE_OFF; v->freq_config_TX.CodeType=CODE_TYPE_OFF;
        v->DTMF_PTT_ID_TX_MODE=PTT_ID_OFF;
    }
    FUNCTION_Select(FUNCTION_FOREGROUND); RADIO_SelectVfos(); RADIO_SetupRegisters(true);
    usb_lost=false;
    gRequestDisplayScreen=DISPLAY_INVALID;
    GUI_SelectNextDisplay(DISPLAY_L4S);
    gUpdateDisplay=true; gUpdateStatus=true;
    return true;
}

static void leave(void)
{
    memcpy(gEeprom.VfoInfo,saved.vfo,sizeof(saved.vfo));
    gEeprom.TX_VFO=saved.tx_vfo; gEeprom.CROSS_BAND_RX_TX=saved.cross;
    gEeprom.DUAL_WATCH=saved.dual; gEeprom.BATTERY_SAVE=saved.battery_save;
    gEeprom.VOX_SWITCH=saved.vox; gEeprom.CW_KEY_INPUT=saved.key_input;
    gEeprom.CW_KEY_WPM=saved.wpm; gEeprom.CW_BREAKIN_ENABLE=saved.breakin;
    gCW_CrossMode=saved.cross_mode;
    RADIO_SelectVfos(); RADIO_SetupRegisters(true); gMonitor=saved.monitor;
    gRequestDisplayScreen=DISPLAY_INVALID;
    GUI_SelectNextDisplay(DISPLAY_MAIN);
    gScanStateDir=saved.scan;
    gUpdateDisplay=true; gUpdateStatus=true;
}

static bool validate_pair(const L4S_Pair *p)
{
    if (RX_freq_check(p->rx_hz/10) != 0) return false;
    if (p->tx_enabled && (RX_freq_check(p->tx_hz/10) != 0 ||
        (gTxVfo->TX_LOCK && TX_freq_check(p->tx_hz/10) != 0))) return false;
    return true;
}

static bool can_retune(void)
{
    return gCurrentFunction != FUNCTION_TRANSMIT && gCW_State == CW_INACTIVE && !gCW_PlaybackActive;
}

static bool apply_pair(const L4S_Pair *p)
{
    if (!can_retune() || !validate_pair(p)) return false;
    const uint32_t f[2]={p->rx_hz/10, p->tx_enabled ? p->tx_hz/10 : p->rx_hz/10};
    for (unsigned i=0;i<2;i++) {
        VFO_Info_t *v=&gEeprom.VfoInfo[i];
        v->freq_config_RX.Frequency=v->freq_config_TX.Frequency=f[i];
        v->Band=FREQUENCY_GetBand(f[i]); v->StepFrequency=1;
        RADIO_ConfigureSquelchAndOutputPower(v);
    }
    RADIO_SetupRegisters(true); gUpdateDisplay=true;
    return true;
}

static bool set_mode(uint8_t rx, uint8_t tx, uint8_t narrow)
{
    static const uint8_t modes[]={MODULATION_FM,MODULATION_AM,MODULATION_USB,MODULATION_CW};
    if (!can_retune() || rx>3 || (tx!=L4S_FM && tx!=L4S_CW_MODE)) return false;
    gRxVfo->Modulation=modes[rx]; gTxVfo->Modulation=modes[tx];
    gRxVfo->CHANNEL_BANDWIDTH=gTxVfo->CHANNEL_BANDWIDTH=narrow ? BANDWIDTH_NARROW : BANDWIDTH_WIDE;
    gRxVfo->freq_config_RX.CodeType=CODE_TYPE_OFF;
    if (tx==L4S_CW_MODE) gTxVfo->freq_config_TX.CodeType=CODE_TYPE_OFF;
    RADIO_SelectVfos(); RADIO_SetupRegisters(true);
    gUpdateDisplay=true; return true;
}

static bool tone(uint16_t tenths)
{
    if (!can_retune()) return false;
    unsigned index=0;
    if (tenths) {
        if (gTxVfo->Modulation != MODULATION_FM) return false;
        for (index=0;index<50 && CTCSS_Options[index]!=tenths;index++);
        if (index==50) return false;
    }
    gTxVfo->freq_config_TX.CodeType=CODE_TYPE_OFF;
    if (tenths) {
        gTxVfo->freq_config_TX.CodeType=CODE_TYPE_CONTINUOUS_TONE;
        gTxVfo->freq_config_TX.Code=index;
    }
    return true;
}

static bool ptt_on(void)
{
    if (!can_retune() || release_latch || gEeprom.KEY_LOCK || (!local_ptt_request && GPIO_IsPttPressed()) ||
        (gTxVfo->BUSY_CHANNEL_LOCK && (g_SquelchLost || gCurrentFunction==FUNCTION_RECEIVE))) return false;
    gPttIsPressed=true; RADIO_PrepareTX();
    if (gCurrentFunction!=FUNCTION_TRANSMIT) { gPttIsPressed=false; return false; }
    gUpdateDisplay=true; return true;
}

static bool cw_start(const uint8_t *text, uint8_t length, uint8_t wpm)
{
    if (!can_retune() || release_latch || GPIO_IsPttPressed() || gEeprom.KEY_LOCK || gBatteryDisplayLevel==0 ||
        gBatteryDisplayLevel>6 || !validate_pair(&state.applied) ||
        (gTxVfo->BUSY_CHANNEL_LOCK && (g_SquelchLost || gCurrentFunction==FUNCTION_RECEIVE))) return false;
    gEeprom.CW_KEY_WPM=wpm;
    if (!CW_StartTextPlayback(text,length)) return false;
    return true;
}

static bool cw_active(void) { return gCW_PlaybackActive; }
static bool cw_failed(void) { return gTxTimeoutReached || VfoState[1]!=VFO_STATE_NORMAL; }
static uint8_t cw_progress(void) { return CW_TextPlaybackPosition(); }
static const L4S_Ops ops={enter,leave,validate_pair,apply_pair,set_mode,tone,ptt_on,stop_tx,
                         cw_start,tx_state,cw_active,cw_failed,cw_progress,can_retune};

void L4S_ControlTick(void)
{
    if (!initialized) { L4S_Init(&state,&ops); initialized=true; }
    if (release_latch && !GPIO_IsPttPressed()) release_latch=false;
    if (state.session) {
        const bool timed_out = state.tx_window_open && (uint32_t)(millis()-state.tx_started) >=
            ((uint32_t)gEeprom.TX_TIMEOUT_TIMER+1)*5000u;
        if (usb_lost) {
            L4S_Abort(&state,L4S_CANCELLED); usb_lost=false;
        } else if (timed_out) {
            L4S_StopTx(&state,L4S_TIMEOUT);
        }
        const bool physical=GPIO_IsPttPressed();
        if (physical!=ptt_raw) { ptt_raw=physical; ptt_changed_ms=millis(); }
        if (physical!=ptt_down && (!physical || (uint32_t)(millis()-ptt_changed_ms)>=30u)) {
            ptt_down=physical;
            local_ptt_request=physical;
            const uint8_t result=L4S_LocalPtt(&state,physical,millis());
            local_ptt_request=false;
            if (physical && result!=L4S_APPLIED) release_latch=true;
        }
    } else ptt_down=ptt_raw=GPIO_IsPttPressed();
    L4S_Tick(&state,millis());
    if (state.session && (uint32_t)(millis()-last_display_ms)>=250u) {
        last_display_ms=millis(); gUpdateDisplay=true; gUpdateStatus=true;
    }
}

bool L4S_LocalKey(bool pressed)
{
    if (key_release_latch) {
        if (!pressed) key_release_latch=false;
        return true;
    }
    if (!pressed || !L4S_Active()) return false;
    key_release_latch=true;
    L4S_Abort(&state,L4S_CANCELLED); return true;
}

void L4S_ProcessKeys(KEY_Code_t key, bool pressed, bool held)
{
    (void)key; (void)held;
    if (!L4S_LocalKey(pressed) && !L4S_Active()) GUI_SelectNextDisplay(DISPLAY_MAIN);
}

bool L4S_HandleUart(uint32_t port, const uint8_t *body, size_t size)
{
    uint16_t cmd=size>=2 ? L4S_Read16(body) : 0;
    if (cmd<L4S_HELLO || cmd>L4S_CONTEXT) {
        if (L4S_Active()) L4S_Abort(&state,L4S_CANCELLED);
        return false;
    }
    if (port!=UART_PORT_VCP) return true;
    L4S_ControlTick();
    uint8_t reply[L4S_MAX_BODY];
    static uint8_t frame[L4S_MAX_BODY+8];
    size_t n=L4S_Handle(&state,body,size,millis(),reply);
    if (n) VCP_Send(frame,L4S_EncodeFrame(reply,n,frame));
    return true;
}
