/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#include "l4s_store.h"
#include "l4s_protocol.h"
#include <string.h>
static uint32_t crc32(uint32_t crc,const uint8_t *p,unsigned n) {
    while(n--) {crc^=*p++;for(unsigned i=0;i<8;i++)crc=(crc>>1)^((crc&1) ? 0xedb88320u : 0);}
    return crc;
}
static bool blank(const uint8_t *p,unsigned n) {while(n--)if(*p++!=0xff)return false;return true;}
static uint32_t source(const L4S_Store *s,unsigned slot) {
    return s->source_legacy ? L4S_SAT_PROFILE_ADDR+slot*64u : s->active+L4S_BANK_HEADER+slot*64u;
}
void L4S_StoreRead(const L4S_Store *s,unsigned slot,uint8_t record[64]) {
    if(slot>=56 || s->ops->busy()) {memset(record,0xff,64);return;}
    s->ops->read(source(s,slot),record,64);
}
static bool bank_valid(L4S_Store *s,uint32_t base,uint32_t *generation) {
    uint8_t h[32];s->ops->read(base,h,32);
    if(memcmp(h,"L4SB",4) || h[4]!=1 || h[5]!=56 || h[31]!=0xa5 ||
       L4S_Read32(h+16)!=~crc32(0xffffffffu,h,16)) return false;
    uint32_t crc=0xffffffffu;
    for(unsigned offset=0;offset<56u*64;offset+=256) {
        s->ops->read(base+256+offset,s->page,256);crc=crc32(crc,s->page,256);
    }
    if(~crc!=L4S_Read32(h+12)) return false;
    *generation=L4S_Read32(h+8);return true;
}
static bool bank_owned(L4S_Store *s,uint32_t base) {
    uint8_t h[5];s->ops->read(base,h,5);
    if(!memcmp(h,"L4SB",4) && h[4]==1) return true;
    for(unsigned offset=0;offset<4096;offset+=256) {
        s->ops->read(base+offset,s->page,256);if(!blank(s->page,256))return false;
    }
    return true;
}
void L4S_StoreInit(L4S_Store *s,const L4S_StoreOps *ops) {
    memset(s,0,sizeof(*s));s->ops=ops;
    uint32_t a=0,b=0;bool av=bank_valid(s,L4S_BANK_A,&a),bv=bank_valid(s,L4S_BANK_B,&b);
    if(av && (!bv || (int32_t)(a-b)>=0)) {s->active=L4S_BANK_A;s->generation=a;}
    else if(bv) {s->active=L4S_BANK_B;s->generation=b;}
    else s->source_legacy=true;
    s->read_only=!bank_owned(s,L4S_BANK_A) || !bank_owned(s,L4S_BANK_B);
    if(!av && !bv) {
        uint8_t header[32];
        for(unsigned bank=0;bank<2;bank++) {
            s->ops->read(bank ? L4S_BANK_B : L4S_BANK_A,header,32);
            if(header[31]==0xa5)s->read_only=true; /* A committed but damaged bank requires explicit recovery. */
        }
    }
    uint8_t record[64];
    for(unsigned i=0;i<56;i++){L4S_StoreRead(s,i,record);if(!blank(record,64))s->used++;}
    s->ready=true;
}
bool L4S_StoreBusy(const L4S_Store *s) {
    return s->pending || (s->phase>=L4S_STORE_ERASE && s->phase<=L4S_STORE_COMMIT);
}
static uint8_t begin(L4S_Store *s,uint32_t expected,uint32_t transaction,uint8_t kind,bool confirm) {
    if(L4S_StoreBusy(s))return 4;
    if(expected!=s->generation)return 9;
    if(s->read_only || !transaction)return 12;
    s->destination=s->active==L4S_BANK_A ? L4S_BANK_B : L4S_BANK_A;
    if(!bank_owned(s,s->destination)){s->read_only=true;return 12;}
    s->transaction=transaction;s->kind=kind;s->auto_confirm=confirm;s->cancel=false;s->committing=false;
    s->slot=0;s->offset=0;s->progress=s->error=s->added=s->replaced=s->deleted=0;
    s->crc=s->verify_crc=0xffffffffu;s->phase=L4S_STORE_ERASE;
    return 1;
}
uint8_t L4S_StoreUpsert(L4S_Store *s,const uint8_t key[16],const char *name,const L4S_UserConfig *c,uint32_t transaction) {
    if(!L4S_ValidConfig(c))return 5;
    uint8_t record[64];int empty=-1,target=-1;
    for(unsigned i=0;i<56;i++) {
        L4S_StoreRead(s,i,record);
        if(blank(record,64)) {if(empty<0)empty=i;}
        else if(!memcmp(record,"L4SM",4) && !memcmp(record+14,key,16)) {
            if(record[4]!=1 && record[4]!=2)return 12;
            target=i;break;
        }
    }
    if(target<0)target=empty;
    if(target<0)return 13;
    uint8_t encoded[64];L4S_EncodeSatelliteRecord(key,name,c,encoded);L4S_StoreRead(s,target,record);
    if(s->ops->valid && !s->ops->valid(encoded))return 5;
    if(!s->source_legacy && !memcmp(record,encoded,64))return 0;
    uint8_t result=begin(s,s->generation,transaction,1,true);if(result!=1)return result;
    s->change_slot=target;memcpy(s->change,encoded,64);return 1;
}
uint8_t L4S_StoreDelete(L4S_Store *s,const uint8_t key[16],uint32_t expected,uint32_t transaction) {
    uint8_t record[64];int target=-1;
    for(unsigned i=0;i<56;i++){L4S_StoreRead(s,i,record);if(!blank(record,64)&&!memcmp(record+14,key,16)){target=i;break;}}
    if(target<0)return 5;
    uint8_t result=begin(s,expected,transaction,2,false);if(result!=1)return result;
    s->change_slot=target;memset(s->change,0xff,64);return 1;
}
uint8_t L4S_StoreBeginImport(L4S_Store *s,uint32_t expected,uint32_t transaction) {return begin(s,expected,transaction,3,false);}
static uint8_t delete_slot(L4S_Store *s,unsigned slot,const uint8_t key[16],uint32_t expected,uint32_t transaction) {
    if(slot>=56)return 5;
    uint8_t record[64];L4S_StoreRead(s,slot,record);
    if(blank(record,64) || memcmp(record+14,key,16))return 9;
    uint8_t result=begin(s,expected,transaction,2,false);if(result!=1)return result;
    s->change_slot=slot;memset(s->change,0xff,64);return 1;
}
uint8_t L4S_StorePut(L4S_Store *s,unsigned slot,const uint8_t record[64],uint32_t transaction) {
    if(transaction!=s->transaction)return 9;
    if(s->phase!=L4S_STORE_UPLOAD || s->pending)return 4;
    if(slot!=s->slot || slot>=56)return 9;
    L4S_UserConfig c;uint8_t old[64];L4S_StoreRead(s,slot,old);
    if(!blank(record,64) && !L4S_DecodeSatelliteRecord(record,&c) && memcmp(old,record,64))return 5;
    if(!blank(record,64) && memcmp(old,record,64) && s->ops->valid && !s->ops->valid(record))return 5;
    if(!blank(old,64) && memcmp(old,record,64) &&
        (!L4S_DecodeSatelliteRecord(old,&c) || blank(record,64) || memcmp(old+14,record+14,16)))return 5;
    if(!blank(record,64))for(unsigned i=0;i<slot;i++) {
        uint8_t prior[64];
        if(i/4==slot/4)memcpy(prior,s->page+(i%4)*64,64);
        else s->ops->read(s->destination+256+i*64,prior,64);
        if(!blank(prior,64) && !memcmp(prior+14,record+14,16))return 5;
    }
    memcpy(s->page+(slot%4)*64,record,64);s->crc=crc32(s->crc,record,64);s->slot++;
    if(s->slot%4==0) {s->offset=256+(s->slot-4)*64;s->phase=L4S_STORE_COPY;}
    return 0;
}
uint8_t L4S_StoreFinish(L4S_Store *s,uint32_t transaction) {
    if(transaction!=s->transaction)return 9;
    if(s->phase!=L4S_STORE_UPLOAD || s->slot!=56 || s->pending)return 4;
    s->offset=0;s->phase=L4S_STORE_VERIFY;return 1;
}
void L4S_StoreConfirm(L4S_Store *s) {if(s->phase==L4S_STORE_CONFIRM)s->phase=L4S_STORE_HEADER;}
void L4S_StoreCancel(L4S_Store *s) {if(L4S_StoreBusy(s) && !s->committing)s->cancel=true;}
static void fail(L4S_Store *s) {s->phase=L4S_STORE_FAILED;s->error=12;}
static bool program(L4S_Store *s,uint32_t address,const void *data,unsigned n,uint32_t now) {
    if(!s->ops->program(address,data,n)){fail(s);return false;}s->pending=true;s->io_started=now;return true;
}
void L4S_StoreTick(L4S_Store *s,uint32_t now) {
    if(s->pending) {
        if(s->ops->busy()){if((uint32_t)(now-s->io_started)>3000)fail(s);return;}
        s->pending=false;
    }
    if(s->cancel && s->phase!=L4S_STORE_DONE){s->phase=L4S_STORE_CANCELLED;s->cancel=false;return;}
    switch(s->phase) {
    case L4S_STORE_ERASE:
        if(!s->ops->erase(s->destination)){fail(s);break;}
        s->pending=true;s->io_started=now;s->phase=L4S_STORE_OWNER;break;
    case L4S_STORE_OWNER:
        memset(s->page,0xff,32);memcpy(s->page,"L4SB",4);s->page[4]=1;s->page[5]=56;
        L4S_Write32(s->page+8,s->generation+1);
        if(program(s,s->destination,s->page,32,now)) {s->offset=256;s->phase=s->kind==3 ? L4S_STORE_UPLOAD : L4S_STORE_COPY;}
        break;
    case L4S_STORE_COPY:
        if(s->kind!=3) {
            for(unsigned i=0;i<4;i++) {
                unsigned index=(s->offset-256)/64+i;uint8_t *record=s->page+i*64;
                L4S_StoreRead(s,index,record);if(index==s->change_slot)memcpy(record,s->change,64);
            }
            s->crc=crc32(s->crc,s->page,256);
        }
        if(program(s,s->destination+s->offset,s->page,256,now)) {
            s->offset+=256;s->progress=(s->offset-256)*70/(56*64);
            if(s->kind==3)s->phase=L4S_STORE_UPLOAD;
            else if(s->offset==256+56*64){s->offset=0;s->phase=L4S_STORE_VERIFY;}
        }
        break;
    case L4S_STORE_VERIFY: {
        uint8_t old[64];s->ops->read(s->destination+256+s->offset,s->page,256);
        s->verify_crc=crc32(s->verify_crc,s->page,256);
        for(unsigned i=0;i<4;i++) {
            L4S_StoreRead(s,s->offset/64+i,old);uint8_t *record=s->page+i*64;
            if(memcmp(old,record,64)) {
                if(blank(old,64))s->added++;else if(blank(record,64))s->deleted++;else s->replaced++;
            }
        }
        s->offset+=256;s->progress=70+s->offset*20/(56*64);
        if(s->offset==56*64) {
            if(s->crc!=s->verify_crc){fail(s);break;}
            s->phase=s->auto_confirm ? L4S_STORE_HEADER : L4S_STORE_CONFIRM;
        }break;
    }
    case L4S_STORE_HEADER:
        memset(s->page,0xff,32);memcpy(s->page,"L4SB",4);s->page[4]=1;s->page[5]=56;
        L4S_Write32(s->page+8,s->generation+1);L4S_Write32(s->page+12,~s->crc);
        L4S_Write32(s->page+16,~crc32(0xffffffffu,s->page,16));
        if(program(s,s->destination,s->page,32,now))s->phase=L4S_STORE_COMMIT;
        break;
    case L4S_STORE_COMMIT: {
        uint8_t header[32];s->ops->read(s->destination,header,32);
        if(header[31]==0xff && !s->committing) {
            if(memcmp(header,s->page,31)){fail(s);break;}
            s->committing=true;s->page[31]=0xa5;program(s,s->destination+31,s->page+31,1,now);break;
        }
        uint32_t generation;
        if(!bank_valid(s,s->destination,&generation)){fail(s);break;}
        s->active=s->destination;s->source_legacy=false;s->generation=generation;s->used=0;
        for(unsigned i=0;i<56;i++){L4S_StoreRead(s,i,s->page);if(!blank(s->page,64))s->used++;}
        s->phase=L4S_STORE_DONE;s->progress=100;break;
    }
    default:break;
    }
}
unsigned L4S_StoreStatus(const L4S_Store *s,uint16_t step,uint8_t out[20]) {
    memset(out,0,20);out[0]=1;out[1]=s->phase;out[2]=s->progress;out[3]=s->error;
    out[4]=(L4S_StoreBusy(s)?1:0)|(s->read_only?2:0)|(s->pending?4:0);out[5]=s->used;out[6]=56;
    L4S_Write32(out+8,s->generation);L4S_Write32(out+12,s->transaction);
    out[16]=s->slot;L4S_Write16(out+17,step);out[19]=s->kind;return 20;
}
uint8_t L4S_StoreCommand(L4S_Store *s,const uint8_t *p,size_t n,bool allowed,uint16_t *step) {
    if(!n)return 2;
    static const uint8_t lengths[]={1,2,9,70,5,26,5,3};
    if(p[0]>7)return 6;
    if(n!=lengths[p[0]])return 2;
    if(p[0]==0)return 0;
    if(p[0]==1)return p[1]>=56 ? 5 : s->ops->busy() ? 4 : 0;
    if(p[0]==6){if(L4S_Read32(p+1)!=s->transaction)return 9;L4S_StoreCancel(s);return 0;}
    if(!allowed)return 4;
    switch(p[0]) {
    case 2:return L4S_StoreBeginImport(s,L4S_Read32(p+1),L4S_Read32(p+5));
    case 3:return L4S_StorePut(s,p[5],p+6,L4S_Read32(p+1));
    case 4:return L4S_StoreFinish(s,L4S_Read32(p+1));
    case 5:return delete_slot(s,p[25],p+9,L4S_Read32(p+1),L4S_Read32(p+5));
    case 7: {
        uint16_t value=L4S_Read16(p+1);if(value<10 || value>1000 || value%10)return 5;
        if(L4S_StoreBusy(s))return 4;
        *step=value;return 0;
    }
    default:return 6;
    }
}
size_t L4S_StoreReply(const L4S_Store *s,const uint8_t *p,uint16_t step,uint8_t *out) {
    L4S_StoreStatus(s,step,out);out[20]=s->added;out[21]=s->replaced;out[22]=s->deleted;
    if(p[0]==1){out[23]=p[1];L4S_StoreRead(s,p[1],out+24);return 88;}return 23;
}
