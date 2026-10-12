/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_L4S_STORE_H
#define APP_L4S_STORE_H
#include "l4s_settings.h"
#include <stddef.h>
#define L4S_BANK_A 0x012000u
#define L4S_BANK_B 0x013000u
#define L4S_BANK_SIZE 4096u
#define L4S_BANK_HEADER 256u
enum { L4S_STORE_IDLE, L4S_STORE_ERASE, L4S_STORE_OWNER, L4S_STORE_COPY,
    L4S_STORE_UPLOAD, L4S_STORE_VERIFY, L4S_STORE_CONFIRM, L4S_STORE_HEADER,
    L4S_STORE_COMMIT, L4S_STORE_DONE, L4S_STORE_CANCELLED, L4S_STORE_FAILED };
typedef struct {
    void (*read)(uint32_t,void *,unsigned);
    bool (*erase)(uint32_t);
    bool (*program)(uint32_t,const void *,unsigned);
    bool (*busy)(void);
    bool (*valid)(const uint8_t record[64]);
} L4S_StoreOps;
typedef struct {
    const L4S_StoreOps *ops;
    uint32_t active, generation, destination, transaction, crc, verify_crc, io_started;
    uint16_t offset;
    uint8_t phase,error,progress,slot,used,kind,added,replaced,deleted,change_slot;
    bool pending,cancel,auto_confirm,source_legacy,read_only,ready,committing;
    uint8_t page[256],change[64];
} L4S_Store;
void L4S_StoreInit(L4S_Store *,const L4S_StoreOps *);
void L4S_StoreTick(L4S_Store *,uint32_t now);
bool L4S_StoreBusy(const L4S_Store *);
void L4S_StoreRead(const L4S_Store *,unsigned slot,uint8_t record[64]);
uint8_t L4S_StoreUpsert(L4S_Store *,const uint8_t key[16],const char *,const L4S_UserConfig *,uint32_t transaction);
uint8_t L4S_StoreDelete(L4S_Store *,const uint8_t key[16],uint32_t expected,uint32_t transaction);
uint8_t L4S_StoreBeginImport(L4S_Store *,uint32_t expected,uint32_t transaction);
uint8_t L4S_StorePut(L4S_Store *,unsigned slot,const uint8_t record[64],uint32_t transaction);
uint8_t L4S_StoreFinish(L4S_Store *,uint32_t transaction);
void L4S_StoreConfirm(L4S_Store *);
void L4S_StoreCancel(L4S_Store *);
unsigned L4S_StoreStatus(const L4S_Store *,uint16_t correction_step,uint8_t out[20]);
uint8_t L4S_StoreCommand(L4S_Store *,const uint8_t *,size_t,bool allowed,uint16_t *correction_step);
size_t L4S_StoreReply(const L4S_Store *,const uint8_t *,uint16_t,uint8_t *);
#endif
