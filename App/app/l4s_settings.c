/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_settings.h"
#include "l4s_protocol.h"
#include "dcs.h"
#include "external/printf/printf.h"
#include <string.h>
_Static_assert(sizeof(L4S_UserConfig)==9 && offsetof(L4S_UserConfig,squelch)==8,"Profile layout changed");
static uint8_t profile_key[16];
void L4S_SetProfileKey(const uint8_t key[16]) {memcpy(profile_key,key,16);}
const uint8_t *L4S_GetProfileKey(void) {return profile_key;}
_Static_assert(L4S_PROFILE_ADDR>=0x00A171u && L4S_PROFILE_ADDR+L4S_PROFILE_SIZE<=0x00B000u,"Profile overlaps legacy settings/calibration");
void L4S_DefaultConfig(L4S_UserConfig *c) { *c=(L4S_UserConfig){0,7,20,60,4,0,0,0,0}; }
bool L4S_ValidConfig(const L4S_UserConfig *c)
{
    return c->mode<=3 && c->power<=7 && c->wpm>=10 && c->wpm<=45 && c->pitch>=45 &&
        c->pitch<=120 && c->pitch%5==0 && c->volume<=6 && c->tx_tone<=50 && c->rx_tone<=50 && c->arm_tone<=50 && c->squelch<=9;
}
void L4S_EncodeProfile(const L4S_UserConfig *c,uint8_t out[L4S_PROFILE_SIZE])
{
    memset(out,0,L4S_PROFILE_SIZE);memcpy(out,"L4SP",4);out[4]=2;memcpy(out+5,c,8);memcpy(out+13,profile_key,16);
    out[29]=c->squelch;
    L4S_Write16(out+30,L4S_Crc(out,30));
}
bool L4S_DecodeProfile(const uint8_t data[L4S_PROFILE_SIZE],L4S_UserConfig *c)
{
    L4S_UserConfig candidate;
    if (memcmp(data,"L4SP",4) || (data[4]!=1 && data[4]!=2) ||
        (data[4]==1 && data[29]!=0) || L4S_Read16(data+30)!=L4S_Crc(data,30)) return false;
    memcpy(&candidate,data+5,8);
    candidate.squelch=data[4]==1 ? 0 : data[29];
    if (!L4S_ValidConfig(&candidate)) return false;
    *c=candidate;memcpy(profile_key,data+13,16);return true;
}
bool L4S_SaveProfile(const L4S_UserConfig *c,void (*read)(uint32_t,void *,unsigned),
                     void (*write)(uint32_t,const void *,unsigned))
{
    uint8_t previous[L4S_PROFILE_SIZE],record[L4S_PROFILE_SIZE],verify[L4S_PROFILE_SIZE];
    if(!L4S_ValidConfig(c)) return false;
    read(L4S_PROFILE_ADDR,previous,sizeof(previous));
    bool blank=true;
    for(unsigned i=0;i<sizeof(previous);i++) if(previous[i]!=0xff) blank=false;
    if(!blank && (memcmp(previous,"L4SP",4) || (previous[4]!=1 && previous[4]!=2))) return false;
    L4S_EncodeProfile(c,record);
    if(memcmp(record,previous,sizeof(record))) write(L4S_PROFILE_ADDR,record,sizeof(record));
    read(L4S_PROFILE_ADDR,verify,sizeof(verify));
    return memcmp(record,verify,sizeof(record))==0;
}
uint8_t L4S_CtcssChoice(uint16_t tenths)
{
    if (!tenths) return 0;
    for (unsigned i=0;i<50;i++) if (CTCSS_Options[i]==tenths) return i+1;
    return 255;
}
uint16_t L4S_CtcssValue(uint8_t choice) { return choice && choice<=50 ? CTCSS_Options[choice-1] : 0; }
void L4S_FormatTone(uint16_t tone,char *text,unsigned size)
{
    if (tone) snprintf_(text,size,"%u.%u",tone/10,tone%10);
    else snprintf_(text,size,"off");
}
static uint8_t change(uint8_t value,int direction,unsigned low,unsigned high,unsigned step)
{
    int next=(int)value+direction*(int)step;
    return next<(int)low ? high : next>(int)high ? low : (unsigned)next;
}
bool L4S_MenuKey(L4S_MenuState *m,KEY_Code_t key,bool busy)
{
    if (key==KEY_EXIT) { m->open=false;m->editing=false;return false; }
    if (busy) m->editing=false;
    if (key==KEY_MENU) {
        if (busy) {m->error=4;return false;}
        if (m->index==L4S_MENU_SAVE) return true;
        m->editing=!m->editing;return false;
    }
    if (key!=KEY_UP && key!=KEY_DOWN) return false;
    int direction=key==KEY_UP ? 1 : -1;
    if (!m->editing) {m->index=change(m->index,-direction,0,L4S_MENU_ITEMS-1,1);return false;}
    m->error=0;
    switch(m->index) {
        case 0:m->draft.mode=change(m->draft.mode,direction,0,3,1);break;
        case 1:m->draft.power=change(m->draft.power,direction,0,7,1);break;
        case 2:m->draft.tx_tone=change(m->draft.tx_tone,direction,0,50,1);break;
        case 3:m->draft.rx_tone=change(m->draft.rx_tone,direction,0,50,1);break;
        case 4:m->draft.arm_tone=change(m->draft.arm_tone,direction,0,50,1);break;
        case 5:m->arm_next=!m->arm_next;break;
        case 6:m->draft.wpm=change(m->draft.wpm,direction,10,45,1);break;
        case 7:m->draft.pitch=change(m->draft.pitch,direction,45,120,5);break;
        case 8:m->draft.volume=change(m->draft.volume,direction,0,6,1);break;
        case 9:m->draft.squelch=change(m->draft.squelch,direction,0,9,1);break;
    }
    return false;
}
void L4S_MenuValue(const L4S_MenuState *m,unsigned item,char *text,unsigned size)
{
    static const char *modes[]={"FM","CW","USB/CW","NFM"};
    static const char *power[]={"USER","LOW1","LOW2","LOW3","LOW4","LOW5","MID","HIGH"};
    char tone[8];
    switch(item) {
        case 0:snprintf_(text,size,"MODE %s",modes[m->draft.mode]);break;
        case 1:snprintf_(text,size,"POWER %s",power[m->draft.power]);break;
        case 2:case 3:case 4:
            L4S_FormatTone(L4S_CtcssValue(item==2 ? m->draft.tx_tone : item==3 ? m->draft.rx_tone : m->draft.arm_tone),tone,sizeof(tone));
            snprintf_(text,size,"%s CTCSS %s",item==2 ? "TX" : item==3 ? "RX" : "ARM",tone);break;
        case 5:snprintf_(text,size,"ARM NEXT %s",m->arm_next ? "ON" : "OFF");break;
        case 6:snprintf_(text,size,"CW WPM %u",m->draft.wpm);break;
        case 7:snprintf_(text,size,"CW TONE %u Hz",m->draft.pitch*10);break;
        case 8:snprintf_(text,size,"CW VOLUME %u",m->draft.volume);break;
        case 9:snprintf_(text,size,"SQUELCH %u%s",m->draft.squelch,m->draft.squelch ? "" : " OFF");break;
        default:snprintf_(text,size,"APPLY + SAVE");break;
    }
}
