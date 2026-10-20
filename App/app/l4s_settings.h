/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_L4S_SETTINGS_H
#define APP_L4S_SETTINGS_H
#include <stdint.h>
#include <stdbool.h>
#include "driver/keyboard.h"
#define L4S_PROFILE_SIZE 32u
#define L4S_MENU_ITEMS 18u
#define L4S_MENU_SAVE 17u
#define L4S_SAT_PROFILE_ADDR 0x00A200u
#define L4S_SAT_PROFILE_SIZE 64u
#define L4S_SAT_PROFILE_COUNT 56u
#define L4S_PROFILE_ADDR 0x00A180u /* Unallocated tail of settings sector; calibration is at 0x010000. */
typedef struct {
    uint8_t mode, power, wpm, pitch, volume, tx_tone, rx_tone, arm_tone, squelch;
    uint32_t rx_base_hz, tx_base_hz;
    int32_t rx_correction_hz, tx_correction_hz;
    uint8_t cw_hang, cw_keyer, cw_spacing;
} L4S_UserConfig;
typedef struct {
    L4S_UserConfig draft;
    bool open, editing, arm_next;
    uint8_t index, error;
    uint16_t step_hz;
    uint16_t correction_step_hz;
} L4S_MenuState;
void L4S_DefaultConfig(L4S_UserConfig *config);
/* Preserve the protocol/profile 0-6 loudness curve with the native 0-15 gain scale. */
uint8_t L4S_NativeSidetoneLevel(uint8_t volume);
bool L4S_ValidConfig(const L4S_UserConfig *config);
bool L4S_ConfigEqual(const L4S_UserConfig *,const L4S_UserConfig *);
void L4S_EncodeProfile(const L4S_UserConfig *config, uint8_t out[L4S_PROFILE_SIZE]);
bool L4S_DecodeProfile(const uint8_t data[L4S_PROFILE_SIZE], L4S_UserConfig *config);
void L4S_SetProfileKey(const uint8_t key[16]);
const uint8_t *L4S_GetProfileKey(void);
bool L4S_SaveProfile(const L4S_UserConfig *config, void (*read)(uint32_t,void *,unsigned),
                     void (*write)(uint32_t,const void *,unsigned));
bool L4S_LoadSatelliteProfile(const uint8_t key[16], L4S_UserConfig *config,
                            void (*read)(uint32_t,void *,unsigned));
bool L4S_DecodeSatelliteRecord(const uint8_t data[64],L4S_UserConfig *config);
void L4S_EncodeSatelliteRecord(const uint8_t key[16],const char *name,const L4S_UserConfig *config,uint8_t out[64]);
/* 0 saved, 12 storage/verification error, 13 full; never evicts another satellite. */
uint8_t L4S_SaveSatelliteProfile(const uint8_t key[16], const char *name, const L4S_UserConfig *config,
    void (*read)(uint32_t,void *,unsigned), void (*write)(uint32_t,const void *,unsigned));
uint8_t L4S_CtcssChoice(uint16_t tenths);
uint16_t L4S_CtcssValue(uint8_t choice);
void L4S_FormatTone(uint16_t tenths, char *text, unsigned size);
/* MENU enters edit; arrows navigate/edit; final item applies; EXIT cancels without ending session. */
bool L4S_MenuKey(L4S_MenuState *menu, KEY_Code_t key, bool busy);
void L4S_MenuValue(const L4S_MenuState *menu, unsigned item, char *text, unsigned size);
uint8_t L4S_ShortcutMenuIndex(KEY_Code_t key);
#endif
