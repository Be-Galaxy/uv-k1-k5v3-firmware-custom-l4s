/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s.h"
#include "app/l4s_control.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "ui/helper.h"

void UI_DisplayL4s(void)
{
    const L4S_State *s=L4S_GetState();
    static const char *modes[]={"FM","AM","USB","CW"};
    char text[24];
    UI_DisplayClear();
    snprintf_(text,sizeof(text),"L4S DOPPLER / %s",modes[s->rx_mode<=3 ? s->rx_mode : 0]);
    UI_PrintStringSmallNormal(text,0,0,0);
    snprintf_(text,sizeof(text),"RX %lu.%05lu",(unsigned long)(s->applied.rx_hz/1000000u),
              (unsigned long)(s->applied.rx_hz%1000000u/10u));
    UI_PrintString(text,0,0,1,8);
    if (s->applied.tx_enabled)
        snprintf_(text,sizeof(text),"TX %lu.%05lu",(unsigned long)(s->applied.tx_hz/1000000u),
                  (unsigned long)(s->applied.tx_hz%1000000u/10u));
    else snprintf_(text,sizeof(text),"TX ---.-----");
    UI_PrintString(text,0,0,3,8);
    if (s->tx_window_open && (s->owner==L4S_TX_CW || s->cw_state==L4S_CW_DONE)) {
        const unsigned progress=s->cw_state==L4S_CW_DONE ? s->cw_length : s->ops->cw_progress();
        snprintf_(text,sizeof(text),"CW %u/%u  TX HOLD",progress,s->cw_length);
    } else if (s->owner==L4S_TX_LOCAL) snprintf_(text,sizeof(text),"LOCAL TX / HOLD");
    else if (s->tx_window_open) snprintf_(text,sizeof(text),"VOICE TX / HOLD");
    else snprintf_(text,sizeof(text),s->pair_ready ? "RX / TRACKING" : "RX / WAIT FREQ");
    UI_PrintStringSmallNormal(text,0,0,5);
    snprintf_(text,sizeof(text),"EXIT:STOP    E:%02u",s->error);
    UI_PrintStringSmallNormal(text,0,0,6);
    ST7565_BlitFullScreen();
}
