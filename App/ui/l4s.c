/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s.h"
#include "app/l4s_control.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "ui/helper.h"
#include <string.h>

static void display_menu(const L4S_State *s,const L4S_MenuState *menu)
{
    char text[24],value[22];
    UI_PrintStringSmallNormal("L4S MENU",0,127,0);
    const unsigned page=menu->index/5*5;
    const unsigned first=page>L4S_MENU_ITEMS-5 ? L4S_MENU_ITEMS-5 : page;
    for(unsigned row=1;row<=5;row++) {
        const unsigned item=first+row-1;
        L4S_MenuValue(menu,item,value,sizeof(value));
        snprintf_(text,sizeof(text),"%c %s",item==menu->index ? (menu->editing ? '*' : '>') : ' ',value);
        UI_PrintStringSmallNormal(text,0,0,row);
    }
    const char *footer=s->tx_window_open ? "TX: VIEW ONLY" : menu->error==13 ? "PROFILE FULL EXIT" : menu->error==12 ? "SAVE ERROR / EXIT" :
        menu->error==3 ? "SET FM + ARM TONE" : "M:EDIT X:CANCEL";
    UI_PrintStringSmallNormal(footer,0,0,6);
    ST7565_BlitFullScreen();
}

void UI_DisplayL4s(void)
{
    const L4S_State *s=L4S_GetState();
    char text[24];
    const char *mode=s->tx_mode==L4S_CW_MODE ? "CW" : "FM";
    UI_DisplayClear();
    const L4S_MenuState *menu=L4S_GetMenu();
    if (menu->open) { display_menu(s,menu); return; }
    snprintf_(text,sizeof(text),"%.15s",s->context_valid && s->satellite_name[0] ? s->satellite_name : "L4S SATELLITE");
    char *suffix=strchr(text,'(');
    if(suffix) {while(suffix>text && suffix[-1]==' ')suffix--;*suffix=0;}
    UI_PrintStringSmallNormal(text,0,0,0);
    UI_PrintStringSmallNormal(mode,112,0,0);
    if (s->context_valid)
        snprintf_(text,sizeof(text),"Az:%03u El:%+03d S%u",s->azimuth_tenths/10,s->elevation_tenths/10,s->settings.squelch);
    else snprintf_(text,sizeof(text),"Az:--- El:--- S%u",s->settings.squelch);
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
    } else {
        char tx[8],rx[8];
        L4S_FormatTone(s->tx_mode==L4S_FM ? L4S_CtcssValue(s->arm_active ? s->settings.arm_tone : s->settings.tx_tone) : 0,tx,sizeof(tx));
        L4S_FormatTone(s->rx_mode==L4S_FM ? L4S_CtcssValue(s->settings.rx_tone) : 0,rx,sizeof(rx));
        snprintf_(text,sizeof(text),"T%s R%s %s",tx,rx,s->tx_window_open ? "HOLD" : s->arm_next ? "ARM" : "MENU");
    }
    if (s->error) snprintf_(text,sizeof(text),"%s E:%02u EXIT:STOP",mode,s->error);
    UI_PrintStringSmallNormal(text,0,0,6);
    ST7565_BlitFullScreen();
}
