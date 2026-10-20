/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_macros.h"
#include "l4s_protocol.h"
#include <string.h>
static bool empty(const uint8_t *p,unsigned n){while(n--)if(*p++!=0xff)return false;return true;}
static bool text_valid(const uint8_t *p,unsigned n){
    for(unsigned i=0;i<n;i++)if(!p[i] || !strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /?.,=-+(&",p[i]))return false;
    return true;
}
static void read_record(const L4S_Macros *s,unsigned slot,uint8_t out[96]){
    memset(out,0,96);if(s->active)s->ops->read(s->active+256+slot*96,out,96);
}
static bool valid_bank(L4S_Macros *s,uint32_t base,uint32_t *generation){
    uint8_t h[32],record[96];s->ops->read(base,h,32);
    if(memcmp(h,"L4MC",4)||h[4]!=1||h[5]!=8||h[6]!=92||h[31]!=0xa5||L4S_Read16(h+14)!=L4S_Crc(h,14))return false;
    uint16_t crc=0;
    for(unsigned i=0;i<8;i++){
        s->ops->read(base+256+i*96,record,96);const uint8_t *r=record;
        crc=L4S_CrcContinue(crc,record,96);if(r[0]>92||!text_valid(r+1,r[0]))return false;
        for(unsigned j=r[0]+1;j<96;j++)if(r[j])return false;
    }
    if(L4S_Read16(h+12)!=crc)return false;
    *generation=L4S_Read32(h+8);return true;
}
static bool owned(L4S_Macros *s,uint32_t base){
    uint8_t page[256];s->ops->read(base,page,32);
    if(!memcmp(page,"L4MC",4)&&page[4]==1)return true;
    for(unsigned n=0;n<4096;n+=256){s->ops->read(base+n,page,256);if(!empty(page,256))return false;}
    return true;
}
void L4S_MacrosInit(L4S_Macros *s,const L4S_StoreOps *ops){
    memset(s,0,sizeof(*s));s->ops=ops;uint32_t a=0,b=0;
    bool av=valid_bank(s,L4S_MACRO_A,&a),bv=valid_bank(s,L4S_MACRO_B,&b);
    if(av&&(!bv||(int32_t)(a-b)>=0)){s->active=L4S_MACRO_A;s->generation=a;}
    else if(bv){s->active=L4S_MACRO_B;s->generation=b;}
    s->read_only=!owned(s,L4S_MACRO_A)||!owned(s,L4S_MACRO_B);
    if(!av&&!bv){uint8_t h[32];for(unsigned i=0;i<2;i++){ops->read(i?L4S_MACRO_B:L4S_MACRO_A,h,32);if(h[31]==0xa5)s->read_only=true;}}
}
bool L4S_MacrosBusy(const L4S_Macros *s){return s->phase>=L4S_MACRO_UPLOAD&&s->phase<=L4S_MACRO_COMMIT;}
uint8_t L4S_MacrosRead(const L4S_Macros *s,unsigned slot,uint8_t text[92]){
    if(slot>=8||s->ops->busy()||L4S_MacrosBusy(s))return 0;
    uint8_t record[96];read_record(s,slot,record);
    if(record[0]>92||!text_valid(record+1,record[0]))return 0;
    memcpy(text,record+1,record[0]);return record[0];
}
void L4S_MacrosConfirm(L4S_Macros *s){if(s->phase==L4S_MACRO_CONFIRM)s->phase=L4S_MACRO_ERASE;}
void L4S_MacrosCancel(L4S_Macros *s){if(L4S_MacrosBusy(s)&&!s->committing)s->cancel=true;}
uint8_t L4S_MacrosCommand(L4S_Macros *s,const uint8_t *p,size_t n,uint32_t session,bool allowed){
    if(!n)return L4S_BAD_LENGTH;
    if(p[0]==0)return n==2&&(p[1]<8||p[1]==255)?0:2;
    if(p[0]==4){if(n!=1)return 2;L4S_MacrosCancel(s);return 0;}
    if(!allowed)return L4S_BUSY;
    if(p[0]==1){
        if(n!=9)return 2;
        if(!L4S_Read32(p+5)||L4S_Read32(p+1)!=s->generation)return 9;
        if(L4S_MacrosBusy(s))return s->session==session&&s->transaction==L4S_Read32(p+5)?1:4;
        if(s->read_only||s->ops->busy())return 12;
        for(unsigned i=0;i<8;i++)read_record(s,i,s->draft+i*96);
        s->session=session;s->transaction=L4S_Read32(p+5);s->received=0;s->error=0;
        s->cancel=s->committing=false;s->phase=L4S_MACRO_UPLOAD;return 1;
    }
    if(s->session!=session)return 3;
    if(p[0]==2){
        if(n<3||p[1]>=8||p[2]>92||n!=(unsigned)p[2]+3)return 2;
        if(s->phase!=L4S_MACRO_UPLOAD)return 4;
        if(!text_valid(p+3,p[2]))return 5;
        uint8_t *r=s->draft+p[1]*96;memset(r,0,96);r[0]=p[2];memcpy(r+1,p+3,p[2]);s->received|=1u<<p[1];return 0;
    }
    if(p[0]==3){if(n!=1)return 2;if(s->phase!=L4S_MACRO_UPLOAD||s->received!=255)return 10;s->phase=L4S_MACRO_CONFIRM;return 1;}
    return 6;
}
size_t L4S_MacrosReply(const L4S_Macros *s,const uint8_t *p,size_t n,uint8_t *out){
    memset(out,0,102);out[0]=1;out[1]=n==2&&p[0]==0?p[1]:255;out[2]=s->phase;out[3]=s->error;
    L4S_Write32(out+4,s->generation);out[8]=s->read_only?1:0;
    if(out[1]<8)out[9]=L4S_MacrosRead(s,out[1],out+10);
    return 10+out[9];
}
static void fail(L4S_Macros *s){s->error=12;s->phase=L4S_MACRO_FAILED;}
static bool program(L4S_Macros *s,uint32_t address,const uint8_t *data,unsigned n,uint32_t now){
    if(!s->ops->program(address,data,n)){fail(s);return false;}
    s->pending=true;s->io_started=now;return true;
}
void L4S_MacrosTick(L4S_Macros *s,uint32_t now,uint32_t session){
    if(!L4S_MacrosBusy(s))return;
    if(session!=s->session&&!s->committing)s->cancel=true;
    if(s->pending){if(s->ops->busy()){if((uint32_t)(now-s->io_started)>3000)fail(s);return;}s->pending=false;}
    if(s->cancel&&!s->committing){s->cancel=false;s->phase=L4S_MACRO_CANCELLED;return;}
    uint32_t dest=s->active==L4S_MACRO_A?L4S_MACRO_B:L4S_MACRO_A;
    switch(s->phase){
    case L4S_MACRO_ERASE:
        if(!owned(s,dest)||!s->ops->erase(dest)){fail(s);break;}
        s->pending=true;s->io_started=now;s->phase=L4S_MACRO_OWNER;break;
    case L4S_MACRO_OWNER:{
        uint8_t h[32];memset(h,0xff,32);memcpy(h,"L4MC",4);h[4]=1;h[5]=8;h[6]=92;h[7]=0;
        L4S_Write32(h+8,s->generation+1);L4S_Write16(h+12,L4S_Crc(s->draft,768));L4S_Write16(h+14,L4S_Crc(h,14));
        if(program(s,dest,h,32,now)){s->offset=0;s->phase=L4S_MACRO_PAGES;}break;}
    case L4S_MACRO_PAGES:
        if(program(s,dest+256+s->offset,s->draft+s->offset,256,now)){s->offset+=256;if(s->offset==768){s->offset=0;s->phase=L4S_MACRO_VERIFY;}}break;
    case L4S_MACRO_VERIFY:{
        uint8_t page[256];s->ops->read(dest+256+s->offset,page,256);
        if(memcmp(page,s->draft+s->offset,256)){fail(s);break;}
        s->offset+=256;if(s->offset==768)s->phase=L4S_MACRO_COMMIT;break;}
    case L4S_MACRO_COMMIT:
        if(!s->committing){const uint8_t mark=0xa5;if(program(s,dest+31,&mark,1,now))s->committing=true;}
        else {uint32_t generation;if(!valid_bank(s,dest,&generation)){fail(s);break;}s->active=dest;s->generation=generation;s->phase=L4S_MACRO_DONE;}
        break;
    default:break;
    }
}
