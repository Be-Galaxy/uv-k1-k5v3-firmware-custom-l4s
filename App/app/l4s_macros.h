/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_L4S_MACROS_H
#define APP_L4S_MACROS_H
#include "l4s_store.h"
#define L4S_MACRO_A 0x014000u
#define L4S_MACRO_B 0x015000u
#define L4S_MACRO_SLOTS 8u
#define L4S_MACRO_TEXT 92u
#define L4S_MACRO_BYTES 96u
enum {L4S_MACRO_IDLE,L4S_MACRO_UPLOAD,L4S_MACRO_CONFIRM,L4S_MACRO_ERASE,
      L4S_MACRO_OWNER,L4S_MACRO_PAGES,L4S_MACRO_VERIFY,L4S_MACRO_COMMIT,
      L4S_MACRO_DONE,L4S_MACRO_CANCELLED,L4S_MACRO_FAILED};
typedef struct {
    const L4S_StoreOps *ops;
    uint32_t active,generation,session,transaction,io_started;
    uint16_t offset;
    uint8_t phase,error,received;
    bool pending,read_only,cancel,committing;
    uint8_t draft[L4S_MACRO_SLOTS*L4S_MACRO_BYTES];
} L4S_Macros;
void L4S_MacrosInit(L4S_Macros *,const L4S_StoreOps *);
bool L4S_MacrosBusy(const L4S_Macros *);
void L4S_MacrosTick(L4S_Macros *,uint32_t now,uint32_t session);
uint8_t L4S_MacrosCommand(L4S_Macros *,const uint8_t *,size_t,uint32_t session,bool allowed);
size_t L4S_MacrosReply(const L4S_Macros *,const uint8_t *,size_t,uint8_t *);
uint8_t L4S_MacrosRead(const L4S_Macros *,unsigned slot,uint8_t text[92]);
void L4S_MacrosConfirm(L4S_Macros *);
void L4S_MacrosCancel(L4S_Macros *);
#endif
