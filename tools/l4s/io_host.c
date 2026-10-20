/* Host-only I/O and profile hooks. Uses the production buffer, key filter and profile codec. */
#include "app/l4s_io.h"
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
/* Kept in lockstep with App/dcs.c by test_io.py; no RF hardware dependencies. */
const uint16_t CTCSS_Options[50]={670,693,719,744,770,797,825,854,885,915,948,974,1000,1035,1072,1109,
1148,1188,1230,1273,1318,1365,1413,1462,1514,1567,1598,1622,1655,1679,1713,1738,1773,1799,1835,
1862,1899,1928,1966,1995,2035,2065,2107,2181,2257,2291,2336,2418,2503,2541};
static L4S_ReplyBuffer writer;
static int submit_result;
static unsigned submits,profile_writes;
static bool eager,corrupt;
static uint8_t record[L4S_PROFILE_SIZE];
static uint8_t satellite_records[L4S_SAT_PROFILE_COUNT*L4S_SAT_PROFILE_SIZE];
void Host_StoreReset(void);
void Host_MacrosReset(void);
static int submit(const uint8_t *data,size_t size) {
    (void)data;(void)size;submits++;
    if(eager) L4S_ReplyComplete(&writer);
    return submit_result;
}
void Host_StorageRead(uint32_t address,void *out,unsigned size) {
    if(address==L4S_PROFILE_ADDR && size==sizeof(record)) memcpy(out,record,size);
    else if(address>=L4S_SAT_PROFILE_ADDR && address+size<=L4S_SAT_PROFILE_ADDR+sizeof(satellite_records))
        memcpy(out,satellite_records+address-L4S_SAT_PROFILE_ADDR,size);
}
void Host_StorageWrite(uint32_t address,const void *data,unsigned size) {
    if(address==L4S_PROFILE_ADDR && size==sizeof(record)) {memcpy(record,data,size);profile_writes++;if(corrupt)record[7]^=1;}
    else if(address>=L4S_SAT_PROFILE_ADDR && address+size<=L4S_SAT_PROFILE_ADDR+sizeof(satellite_records)) {
        memcpy(satellite_records+address-L4S_SAT_PROFILE_ADDR,data,size);profile_writes++;
        if(corrupt)satellite_records[address-L4S_SAT_PROFILE_ADDR+7]^=1;
    }
}
EXPORT void Host_IoReset(void) {memset(&writer,0,sizeof(writer));memset(record,0xff,sizeof(record));memset(satellite_records,0xff,sizeof(satellite_records));submit_result=0;submits=profile_writes=0;eager=corrupt=false;Host_StoreReset();Host_MacrosReset();}
EXPORT bool Host_Send(const uint8_t *data,size_t size,bool dtr) {return L4S_SendReply(&writer,dtr,data,size,submit);}
EXPORT void Host_SendResult(int result,bool immediate) {submit_result=result;eager=immediate;}
EXPORT void Host_ReplyDone(void) {L4S_ReplyComplete(&writer);}
EXPORT bool Host_ReplyBusy(void) {return writer.busy;}
EXPORT const uint8_t *Host_ReplyData(void) {return writer.data;}
EXPORT unsigned Host_Submits(void) {return submits;}
EXPORT uint8_t Host_NativeSidetoneLevel(uint8_t volume) {return L4S_NativeSidetoneLevel(volume);}
EXPORT uint8_t Host_Shortcut(unsigned key) {return L4S_ShortcutMenuIndex(key);}
EXPORT bool Host_SaveConfig(const L4S_UserConfig *c) {return L4S_SaveProfile(c,Host_StorageRead,Host_StorageWrite);}
EXPORT bool Host_ReadConfig(L4S_UserConfig *c) {return L4S_DecodeProfile(record,c);}
EXPORT void Host_UnknownProfile(void) {memset(record,0x55,sizeof(record));}
EXPORT void Host_CorruptWrite(bool value) {corrupt=value;}
EXPORT unsigned Host_ProfileWrites(void) {return profile_writes;}
EXPORT uint16_t Host_CtcssValue(uint8_t choice) {return L4S_CtcssValue(choice);}
EXPORT void Host_ProfileKey(const uint8_t *key) {L4S_SetProfileKey(key);}
EXPORT const uint8_t *Host_ReadProfileKey(void) {return L4S_GetProfileKey();}
EXPORT uint8_t *Host_ProfileData(void) {return record;}
EXPORT void Host_DefaultConfig(L4S_UserConfig *c) {L4S_DefaultConfig(c);}
EXPORT uint8_t Host_SaveSatellite(const uint8_t *key,const L4S_UserConfig *c) {return L4S_SaveSatelliteProfile(key,"SATELLITE",c,Host_StorageRead,Host_StorageWrite);}
EXPORT bool Host_LoadSatellite(const uint8_t *key,L4S_UserConfig *c) {return L4S_LoadSatelliteProfile(key,c,Host_StorageRead);}
EXPORT uint8_t *Host_SatelliteData(void) {return satellite_records;}
