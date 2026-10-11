/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_L4S_SETTINGS_H
#define APP_L4S_SETTINGS_H
#include <stdint.h>
#include <stdbool.h>
#include "driver/keyboard.h"
#define L4S_PROFILE_SIZE 32u
#define L4S_PROFILE_ADDR 0x00A180u /* Unallocated tail of settings sector; calibration is at 0x010000. */
typedef struct {
    uint8_t mode, power, wpm, pitch, volume, tx_tone, rx_tone, arm_tone;
} L4S_UserConfig;
typedef struct {
    L4S_UserConfig draft;
    bool open, editing, arm_next;
    uint8_t index, error;
} L4S_MenuState;
void L4S_DefaultConfig(L4S_UserConfig *config);
bool L4S_ValidConfig(const L4S_UserConfig *config);
void L4S_EncodeProfile(const L4S_UserConfig *config, uint8_t out[L4S_PROFILE_SIZE]);
bool L4S_DecodeProfile(const uint8_t data[L4S_PROFILE_SIZE], L4S_UserConfig *config);
void L4S_SetProfileKey(const uint8_t key[16]);
const uint8_t *L4S_GetProfileKey(void);
bool L4S_SaveProfile(const L4S_UserConfig *config, void (*read)(uint32_t,void *,unsigned),
                     void (*write)(uint32_t,const void *,unsigned));
uint8_t L4S_CtcssChoice(uint16_t tenths);
uint16_t L4S_CtcssValue(uint8_t choice);
void L4S_FormatTone(uint16_t tenths, char *text, unsigned size);
/* MENU enters edit; arrows navigate/edit; final item applies; EXIT cancels without ending session. */
bool L4S_MenuKey(L4S_MenuState *menu, KEY_Code_t key, bool busy);
void L4S_MenuValue(const L4S_MenuState *menu, unsigned item, char *text, unsigned size);
#endif
