/* Copyright 2026 Iu_yang1. SPDX-License-Identifier: Apache-2.0 */
#ifndef APP_L4S_PROTOCOL_H
#define APP_L4S_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "l4s_settings.h"

#define L4S_VERSION 1
#define L4S_TIMEOUT_MS 1500u
#define L4S_MAX_TEXT 92u
#define L4S_META_SIZE 8u
#define L4S_SNAPSHOT_SIZE 24u
#define L4S_MAX_BODY 120u

enum {
    L4S_HELLO = 0x0700, L4S_OPEN, L4S_KEEPALIVE, L4S_CLOSE,
    L4S_STATUS, L4S_PAIR, L4S_MODE, L4S_TONE, L4S_PTT, L4S_CW, L4S_STOP, L4S_CONTEXT, L4S_SETTINGS, L4S_PROFILE
};
enum {
    L4S_APPLIED, L4S_ACCEPTED, L4S_BAD_LENGTH, L4S_BAD_SESSION, L4S_BUSY,
    L4S_RANGE, L4S_UNSUPPORTED, L4S_TX_REJECTED, L4S_CANCELLED, L4S_STALE,
    L4S_NOT_READY, L4S_TIMEOUT
};
enum { L4S_FM, L4S_AM, L4S_USB, L4S_CW_MODE };
enum { L4S_TX_OFF, L4S_TX_VOICE, L4S_TX_CW, L4S_TX_LOCAL };
enum { L4S_CW_IDLE, L4S_CW_QUEUED, L4S_CW_SENDING, L4S_CW_DONE, L4S_CW_CANCELLED, L4S_CW_FAILED };
enum { L4S_CAP_PAIR = 1u, L4S_CAP_PTT = 2u, L4S_CAP_CW = 4u, L4S_CAP_TONE = 8u,
       L4S_CAP_LIVE_FM = 16u, L4S_CAP_LIVE_CW = 32u, L4S_CAP_CONTEXT = 64u, L4S_CAP_SETTINGS = 128u, L4S_CAP_PROFILES = 256u };

typedef struct {
    uint32_t rx_hz, tx_hz, revision;
    bool tx_enabled;
} L4S_Pair;

typedef struct {
    bool (*enter)(void);
    void (*leave)(void);
    bool (*validate_pair)(const L4S_Pair *pair);
    bool (*apply_pair)(const L4S_Pair *pair);
    bool (*mode)(uint8_t rx, uint8_t tx, uint8_t narrow);
    bool (*tone)(uint16_t tenths_hz);
    bool (*ptt_on)(void);
    void (*stop)(void);
    bool (*cw_start)(const uint8_t *text, uint8_t length, uint8_t wpm);
    uint8_t (*tx_state)(void);
    bool (*cw_active)(void);
    bool (*cw_failed)(void);
    uint8_t (*cw_progress)(void);
    bool (*can_retune)(void);
    uint8_t (*profile)(uint8_t operation,const uint8_t *data);
} L4S_Ops;

typedef struct {
    const L4S_Ops *ops;
    L4S_Pair applied;
    uint32_t session, retired_session, last_alive, tx_started;
    uint16_t last_seq, last_cmd, last_hash;
    uint8_t last_result, rx_mode, tx_mode, narrow, owner, cw_state, cw_length, error;
    bool have_seq, pair_ready, mode_ready, tx_window_open;
    char satellite_name[17];
    uint16_t azimuth_tenths;
    int16_t elevation_tenths;
    bool context_valid;
    L4S_UserConfig settings;
    L4S_UserConfig saved_settings;
    uint8_t context_key[16], saved_key[16];
    bool saved_settings_valid, user_modified;
    bool menu_open, arm_next, arm_active;
    bool profile_selected;
    uint16_t step_hz;
    uint32_t profile_generation;
} L4S_State;

void L4S_Init(L4S_State *s, const L4S_Ops *ops);
void L4S_Tick(L4S_State *s, uint32_t now);
void L4S_Abort(L4S_State *s, uint8_t reason);
uint8_t L4S_LocalPtt(L4S_State *s, bool pressed, uint32_t now);
void L4S_StopTx(L4S_State *s, uint8_t reason);
/* Input/output are the unobfuscated body, including legacy ID/size header. */
size_t L4S_Handle(L4S_State *s, const uint8_t *body, size_t length, uint32_t now, uint8_t *reply);
uint16_t L4S_Crc(const uint8_t *data, size_t length);
size_t L4S_EncodeFrame(const uint8_t *body, size_t length, uint8_t *frame);
uint16_t L4S_Read16(const uint8_t *p);
uint32_t L4S_Read32(const uint8_t *p);
void L4S_Write16(uint8_t *p, uint16_t value);
void L4S_Write32(uint8_t *p, uint32_t value);

#endif
