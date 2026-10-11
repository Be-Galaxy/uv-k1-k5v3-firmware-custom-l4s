/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s.h"
#include "app/l4s_control.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "ui/helper.h"

void UI_DisplayL4s(void)
{
    const L4S_State *s=L4S_GetState();
    char text[24];
    UI_DisplayClear();
    UI_PrintStringSmallNormal(s->context_valid && s->satellite_name[0] ? s->satellite_name : "L4S SATELLITE",0,127,0);
    if (s->context_valid)
        snprintf_(text,sizeof(text),"Az:%03u El:%+03d",s->azimuth_tenths/10,s->elevation_tenths/10);
    else snprintf_(text,sizeof(text),"Az:--- El:---");
    UI_PrintStringSmallNormal(text,0,127,1);
    snprintf_(text,sizeof(text),"RX %lu.%05lu",(unsigned long)(s->applied.rx_hz/1000000u),
              (unsigned long)(s->applied.rx_hz%1000000u/10u));
    UI_PrintString(text,0,0,2,8);
    if (s->applied.tx_enabled)
        snprintf_(text,sizeof(text),"TX %lu.%05lu",(unsigned long)(s->applied.tx_hz/1000000u),
                  (unsigned long)(s->applied.tx_hz%1000000u/10u));
    else snprintf_(text,sizeof(text),"TX DISABLED");
    UI_PrintString(text,0,0,4,8);
    if (s->tx_window_open && (s->owner==L4S_TX_CW || s->cw_state==L4S_CW_DONE)) {
        const unsigned progress=s->cw_state==L4S_CW_DONE ? s->cw_length : s->ops->cw_progress();
        snprintf_(text,sizeof(text),"CW %u/%u HOLD EXIT",progress,s->cw_length);
    } else if (s->owner==L4S_TX_LOCAL) snprintf_(text,sizeof(text),"LOCAL TX HOLD EXIT");
    else if (s->tx_window_open) snprintf_(text,sizeof(text),"VOICE TX HOLD EXIT");
    else snprintf_(text,sizeof(text),s->pair_ready ? "RX TRACKING  EXIT" : "RX WAIT FREQ EXIT");
    if (s->error) snprintf_(text,sizeof(text),"E:%02u  EXIT:STOP",s->error);
    UI_PrintStringSmallNormal(text,0,0,6);
    ST7565_BlitFullScreen();
}
