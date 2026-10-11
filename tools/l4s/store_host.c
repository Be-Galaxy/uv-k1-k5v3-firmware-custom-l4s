/* Host NOR model for production A/B transaction tests. */
#include "app/l4s_store.h"
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
void Host_StorageRead(uint32_t,void *,unsigned);
static uint8_t banks[8192];
static L4S_Store store;
static unsigned wait_ticks,writes,erases;
static int cut=-1;
static void read_store(uint32_t address,void *out,unsigned n) {
    if(address>=L4S_BANK_A && address+n<=L4S_BANK_B+4096)memcpy(out,banks+address-L4S_BANK_A,n);
    else Host_StorageRead(address,out,n);
}
static bool busy(void) {return wait_ticks!=0;}
static bool erase(uint32_t address) {
    if(busy() || (address!=L4S_BANK_A && address!=L4S_BANK_B))return false;
    memset(banks+address-L4S_BANK_A,0xff,cut<0 ? 4096 : (unsigned)cut>4096 ? 4096 : (unsigned)cut);
    wait_ticks=2;erases++;return true;
}
static bool program(uint32_t address,const void *data,unsigned n) {
    if(busy() || address<L4S_BANK_A || address+n>L4S_BANK_B+4096 || address%256+n>256)return false;
    const uint8_t *p=data;unsigned count=cut<0 ? n : (unsigned)cut<n ? (unsigned)cut : n;
    for(unsigned i=0;i<count;i++)banks[address-L4S_BANK_A+i]&=p[i];
    wait_ticks=1;writes++;return true;
}
static const L4S_StoreOps ops={read_store,erase,program,busy,NULL};
const L4S_Store *L4S_GetStore(void) {return &store;}
EXPORT void Host_StoreReset(void) {memset(banks,0xff,sizeof(banks));wait_ticks=writes=erases=0;cut=-1;L4S_StoreInit(&store,&ops);}
void Host_IoReset(void);
void Host_StoreEnsure(void) {if(!store.ready)Host_IoReset();}
EXPORT void Host_StoreRestart(void) {wait_ticks=0;L4S_StoreInit(&store,&ops);}
EXPORT void Host_StoreTick(uint32_t now) {if(wait_ticks)wait_ticks--;L4S_StoreTick(&store,now);}
EXPORT void Host_StoreConfirm(void) {L4S_StoreConfirm(&store);}
EXPORT void Host_StoreCancel(void) {L4S_StoreCancel(&store);}
EXPORT void Host_StoreCut(int bytes) {cut=bytes;}
EXPORT const uint8_t *Host_StoreData(void) {return banks;}
EXPORT uint8_t *Host_StoreMutableData(void) {return banks;}
EXPORT unsigned Host_StoreWrites(void) {return writes;}
EXPORT unsigned Host_StoreErases(void) {return erases;}
EXPORT unsigned Host_StorePhase(void) {return store.phase;}
EXPORT unsigned Host_StoreGeneration(void) {return store.generation;}
EXPORT unsigned Host_StoreError(void) {return store.error;}
EXPORT unsigned Host_StoreProgress(void) {return store.progress;}
EXPORT bool Host_StoreUploadReady(void) {return store.phase==L4S_STORE_UPLOAD && !store.pending;}
EXPORT uint8_t Host_StoreUpsert(const uint8_t *key,const L4S_UserConfig *c,uint32_t transaction) {
    return L4S_StoreUpsert(&store,key,"SATELLITE",c,transaction);
}
EXPORT void Host_StoreReadSlot(unsigned slot,uint8_t *out) {L4S_StoreRead(&store,slot,out);}
EXPORT uint8_t Host_StoreCommand(const uint8_t *p,unsigned n,bool allowed,uint16_t *step) {return L4S_StoreCommand(&store,p,n,allowed,step);}
size_t Host_StoreReply(const uint8_t *p,uint16_t step,uint8_t *out) {return L4S_StoreReply(&store,p,step,out);}
