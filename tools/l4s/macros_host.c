/* Host-only NOR model for the production eight-slot macro store. */
#include "app/l4s_macros.h"
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static uint8_t banks[8192];
static L4S_Macros macros;
static unsigned waiting,writes,erases;
static int cut=-1;
static void read_flash(uint32_t address,void *out,unsigned n){memcpy(out,banks+address-L4S_MACRO_A,n);}
static bool busy(void){return waiting!=0;}
static bool erase(uint32_t address){
    if(waiting||(address!=L4S_MACRO_A&&address!=L4S_MACRO_B))return false;
    unsigned n=cut<0?4096:(unsigned)cut>4096?4096:(unsigned)cut;
    memset(banks+address-L4S_MACRO_A,0xff,n);waiting=2;erases++;return true;
}
static bool program(uint32_t address,const void *data,unsigned n){
    if(waiting||address<L4S_MACRO_A||address+n>L4S_MACRO_B+4096||address%256+n>256)return false;
    unsigned count=cut<0?n:(unsigned)cut<n?(unsigned)cut:n;
    for(unsigned i=0;i<count;i++)banks[address-L4S_MACRO_A+i]&=((const uint8_t *)data)[i];
    waiting=1;writes++;return true;
}
static const L4S_StoreOps ops={read_flash,erase,program,busy,NULL};
const L4S_Macros *L4S_GetMacros(void){return &macros;}
EXPORT void Host_MacrosReset(void){memset(banks,0xff,sizeof(banks));waiting=writes=erases=0;cut=-1;L4S_MacrosInit(&macros,&ops);}
EXPORT void Host_MacrosRestart(void){waiting=0;L4S_MacrosInit(&macros,&ops);}
EXPORT void Host_MacrosTick(uint32_t now,uint32_t session){if(waiting)waiting--;L4S_MacrosTick(&macros,now,session);}
EXPORT void Host_MacrosConfirm(void){L4S_MacrosConfirm(&macros);}
EXPORT void Host_MacrosCancel(void){L4S_MacrosCancel(&macros);}
EXPORT uint8_t Host_MacrosCommand(const uint8_t *p,size_t n,uint32_t session,bool allowed){return L4S_MacrosCommand(&macros,p,n,session,allowed);}
EXPORT size_t Host_MacrosReply(const uint8_t *p,size_t n,uint8_t *out){return L4S_MacrosReply(&macros,p,n,out);}
EXPORT unsigned Host_MacrosPhase(void){return macros.phase;}
EXPORT unsigned Host_MacrosGeneration(void){return macros.generation;}
EXPORT unsigned Host_MacrosWrites(void){return writes;}
EXPORT unsigned Host_MacrosErases(void){return erases;}
EXPORT uint8_t *Host_MacrosData(void){return banks;}
EXPORT void Host_MacrosCut(int n){cut=n;}
