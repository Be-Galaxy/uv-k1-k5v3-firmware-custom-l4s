# L4S v1 USB control protocol

The `L4S` preset extends the NR7Y `CW` preset. The original Fusion/CW presets and legacy serial commands remain available. L4S controls one half-duplex UV-K1 / UV-K5 V3; USB carries commands, not audio. Microphone audio stays on the radio/accessory connector.

## Frame and command contract

All integers are little-endian. Frame: `AB CD | body length:u16 | XOR(body + CRC:u16) | DC BA`. CRC is CRC-16/XMODEM (initial 0, polynomial 0x1021), over the plaintext body. XOR repeats `16 6C 14 E6 2E 91 0D 40 21 35 D5 40 13 03 E9 80`. Body length is 12–120 bytes. L4S responses have real CRC; legacy replies retain their existing format.

Body: `command:u16 | payload length:u16 | version:u8=1 | reserved:u8=0 | sequence:u16 | session:u32 | arguments`. Payload length includes the eight metadata bytes. Replies use command OR `0x8000`; the reserved byte becomes the result code. Replies echo the sequence and report the active session (zero after close/timeout). Session identifiers must be fresh, nonzero values. CLOSE and timeout retire the last session. Sequences increase modulo 65536 within a session. Identical retries do not repeat operations or extend the watchdog.

| ID | Operation | Arguments |
|---|---|---|
| 0700 | HELLO | none; session may be zero |
| 0701 | OPEN | none; chosen session in metadata |
| 0702 | KEEPALIVE | none |
| 0703 | CLOSE | none |
| 0704 | STATUS | none |
| 0705 | PAIR | RX Hz:u32, TX Hz:u32, TX enabled:u8, revision:u32 |
| 0706 | MODE | RX mode:u8, TX mode:u8, narrow:u8 (0 wide, 1/2 narrow using radio SetNFM) |
| 0707 | TONE | TX CTCSS in tenths of Hz:u16; zero disables |
| 0708 | PTT | enabled:u8, applied frequency revision:u32 |
| 0709 | CW | WPM:u8, text length:u8, ASCII text |
| 070A | STOP | none; cancels TX, pending frequency, CW; invalidates TX readiness |
| 070B | CONTEXT | azimuth in 0.1 degrees:u16, elevation in 0.1 degrees:i16, zero-padded printable ASCII satellite name:16 bytes; optionally append context identity:16 bytes with feature 128 |
| 070C | SETTINGS | none; requires feature 128; read-only native settings and live CTCSS |

Modes: FM=0, AM=1, USB receive=2, CW=3. TX accepts FM/CW only. PTT-ON is for FM voice; CW uses its own firmware playback engine. Readiness requires PAIR and MODE to have been applied. After STOP/PTT-OFF, apply a newer PAIR revision before requesting another transmission.

Results: APPLIED=0, ACCEPTED=1, BAD_LENGTH=2, BAD_SESSION=3, BUSY=4, RANGE=5, UNSUPPORTED=6, TX_REJECTED=7, CANCELLED=8, STALE=9, NOT_READY=10, TIMEOUT=11. ACCEPTED acknowledges a queued CW operation, not an applied frequency. During voice TX or an entire CW message (including key-up gaps), PAIR returns BUSY: neither RX nor TX may be tuned or queued. After confirmed TX end, the application recomputes a fresh frequency pair for the current time and resumes tracking. The pending-revision field is reserved and remains zero.

## Replies

HELLO data (42 bytes): `L4S1` (4), family:u8=3, features:u32, resolution Hz:u16=10, max text:u8=92, minimum WPM:u8=10, maximum WPM:u8=45, watchdog ms:u16=1500, RX envelope minimum Hz:u32, maximum Hz:u32, firmware label:16 zero-padded ASCII bytes, RX mode mask:u8, TX mode mask:u8. Family 3 denotes the shared UV-K1/K5 V3 firmware, not an individually detected model. The RX envelope includes gaps; every frequency is still validated by the firmware. TX is governed by the existing radio frequency lock and per-channel TX lock.

Feature bits: atomic pair=1, PTT=2, CW text=4, CTCSS=8, optional satellite display context=64, native settings=128. Only send CONTEXT when bit 64 is advertised; earlier v1 firmware is still supported without it. Azimuth must be 0–359.9 degrees and elevation -90–90 degrees. CONTEXT is permitted during TX, never alters frequencies/mode/PTT, and replies with the unchanged 24-byte snapshot. With feature 128 it accepts either the legacy 20-byte arguments or 36 bytes including identity. Android derives identity from the first 16 SHA-256 bytes of the transponder UUID; it is a matching key, not authentication. Azimuth 0xffff plus elevation INT16_MIN primes identity without displaying fake angles. Bits 16/32 are reserved and never advertised: TX/RX Doppler control is prohibited during all transmission, including CW gaps.

Other replies except SETTINGS contain a 24-byte snapshot: RX Hz:u32, TX Hz:u32, applied revision:u32, pending revision:u32, RX mode:u8, TX mode:u8, TX enabled:u8, TX state:u8, CW state:u8, CW character position:u8, CW total:u8, last error:u8. TX state: OFF=0, remote voice=1, CW operation=2, local TX=3. CW state: IDLE=0, QUEUED=1, SENDING=2, DONE=3, CANCELLED=4, FAILED=5. A CW operation includes key-up gaps and boundaries between text chunks. After DONE, send the next CW chunk or STOP to release the operation; neither PAIR nor MODE/TONE is accepted until STOP. The TOT deadline starts at the first chunk and is not reset by later chunks. TX state 2 is an operation reservation, not proof that the carrier is continuously on.

SETTINGS APPLIED data is 55 bytes: snapshot at 0–23; saved configuration (current configuration if no saved profile) at 24–31; actual TX CTCSS:u16 at 32, RX CTCSS:u16 at 34, activation CTCSS:u16 at 36, flags:u8 at 38, saved context identity:16 bytes at 39–54. CTCSS values use tenths of Hz, zero off. Configuration bytes are mode, power, WPM, pitch, volume, TX tone choice, RX tone choice, activation tone choice. Mode: 0 FM, 1 CW, 2 USB-RX/CW-TX, 3 NFM (these configuration codes differ from wire MODE codes). Power: 0 USER through 7 HIGH. WPM: 10–45; pitch: 45–120 in 10 Hz units, step 5; volume: 0–6; tone choices: 0 off, 1–50 in the production CTCSS table. Flags: menu open=1, next local PTT armed=2, activation tone active=4, user modified=8, saved profile valid=16. SETTINGS wire frames are 75 bytes and span multiple USB packets. Replies use an owned 128-byte nonblocking buffer released by USB IN completion; a busy submission is refused, not allowed to overwrite the pending frame.

Frequency values mean intended RF frequency, not a CW BFO/sidetone offset. The firmware rounds to the nearest 10 Hz and applies its existing receive BFO once. Remote sessions temporarily disable CW cross-mode offset. Software state/readback is not a measured RF frequency.

## Operation and recovery

1. Build with `cmake --preset L4S` and `cmake --build --preset L4S`. Output: `build/L4S/l4s.control.bin`, `.uf2`, `.hex`, `.elf`, `.map`.
2. Back up calibration/configuration with the existing UV Studio procedure before any flashing. This development work does not change calibration or bootloader layout; UF2 application base stays `0x08002800`.
3. Select a non-USB CW key-input mode on the radio before connecting USB CDC: the USB paddle/handkey modes use the same PA11/PA12 pins. Use an OTG-capable Android phone and a data cable, not a charging-only cable.
4. HELLO, OPEN, MODE, PAIR, then KEEPALIVE every 500 ms. Confirm the applied revision before PTT/CW. The radio must be on its main screen and idle to open a session. Existing lock, battery, busy-channel and TOT checks remain applicable.
5. Send STOP on button release, touch cancellation, application backgrounding, pass end or error; CLOSE before normal disconnect. Reconnect with a new session and reconfigure. No transmission is automatically resumed.

A 1500 ms watchdog stops TX/CW and restores the original VFO, scan/dual-watch, VOX, battery-save and CW settings. DTR loss also requests shutdown. Alpha4 allows only EXIT on the Doppler main page to abort deliberately; MENU opens native configuration, and unrelated keys are consumed. Physical PTT is debounced for 30 ms, reports TX state 3, holds both frequencies, and retains the session until release. FM uses the microphone; CW uses the existing native carrier path. MODE/PAIR readiness, locks, battery, busy-channel and TOT checks still apply. A press during remote PTT/CW cancels that operation without rekeying on the same press; release and press again to transmit locally. A STOP/TOT while PTT is held latches release, so it cannot rekey automatically. Tracking commands never save VFO/channel/settings to Flash.

MENU uses arrows for selection, MENU for edit/finish edit, arrows for values, and the final APPLY + SAVE item for explicit application. EXIT cancels the draft without closing the session. During TX the menu is read-only; while open it rejects new PTT/CW. Items cover mode, power, TX/RX/activation CTCSS, ARM NEXT, WPM, CW pitch, volume and SQUELCH 0–9. ARM NEXT is not persistent: it only selects the independent activation tone for the next physical FM PTT, never automatically transmits, and normal tone returns on release/stop. CW and USB-RX modes use CW transmission, not SSB transmission.

The native profile is 32 bytes at physical SPI 0x00A180–0x00A19F, after existing settings ending at 0x00A171 and separate from calibration at 0x010000. Alpha5 layout: magic `L4SP` at 0–3, storage version 2 at 4, the original eight configuration bytes at 5–12, context identity at 13–28, squelch level 0–9 at 29, CRC-16/XMODEM over bytes 0–29 at 30–31. Version-1 profiles (reserved byte 29 must be zero) load with squelch off and are upgraded only on explicit save. Save only writes erased or owned version-1/version-2 storage, skips identical bytes and verifies readback. Unknown storage/future versions are not overwritten. An invalid owned CRC can be repaired by explicit save. Only the most recent context is retained; matching mode/tones override transponder defaults, different contexts use new data defaults, and power/CW/squelch settings remain global. Legacy serial backup includes this tail for the L4S preset. Save uses the existing sector read-modify-write and is not power-loss atomic: keep power on and back up configuration before flashing. Doppler and heartbeat never write this profile. Older alpha4 firmware cannot load version-2 profiles; it rejects rather than overwrites them.

Alpha5 defaults satellite squelch to 0 (off), explicitly opening the RX audio path without leaving the Doppler page. MENU can select 0–9; APPLY + SAVE retains the chosen level for later sessions, while EXIT discards the draft. The main page shows S0–S9. Both VFO squelch thresholds are recalculated, including after band/frequency changes. Explicit nonzero squelch overrides the ordinary CW/USB monitor default and remains effective after PTT/CW. Leaving, timing out or disconnecting restores the original normal-mode squelch and monitor state. Other shortcuts remain filtered; no side-key reassignment was needed. SETTINGS remains the same 55-byte wire data as alpha4; the added squelch is handset-local, not an additional Android field. The existing alpha4 signed APK is compatible and does not need reinstalling.

The dedicated 128x56 content-area Doppler screen retains the native 8-pixel status bar, taking its information hierarchy from the [OpenGD77 satellite live data screen](https://github.com/LibreDMR/OpenGD77_UserGuide/blob/master/OpenGD77_User_Guide.md#satellite-live-data-screen). OPEN navigates from the main screen into it; CLOSE/timeout/local EXIT restores the main screen and saved runtime state. Alpha4 always displays FM or CW operation plus TX/RX CTCSS, alongside satellite name, phone-computed azimuth/elevation (including below-horizon values), applied RF RX/TX, tracking versus TX HOLD, CW progress or an error code. No TLE prediction or DMR decoding runs on this radio. `tools/l4s/test_display.py` runs the production renderer with firmware fonts and bounds-checked host drawing hooks. `render_display.py output.png` and `render_display.py output.png --menu` produce host-only previews (optional Pillow dependency), not device screenshots.

Alpha4 also normalizes the CDC receive ring write index at exact wrap-around and removes L4S replies from the old blocking writer that could drop DTR after transient endpoint congestion. Android uses one bounded short retry and includes USB write diagnostics before disconnecting on persistent failure. The watchdog is unchanged; no automatic reconnect/rekey is implemented. Software tests cannot establish whether an observed device disconnect was caused by cable, power or RF interference; static and physical-PTT soak tests remain required.

After any observed TX-to-RX transition, the Android controller invalidates its cached pair readiness and sends a fresh revision before rekeying. HELD in the Android interface means no command was applied or queued during local/remote TX; it is not an additional wire result code. The wire protocol remains v1.

Restore the baseline firmware using the same UV Studio/UF2 procedure and the saved configuration/calibration if rollback is needed. Do not restore incompatible memory layouts without the original firmware's migration procedure. Default application image size must stay within 118 KiB; the linker enforces the 16 KiB RAM region including reserved stack/heap.

## Host verification

`tools/l4s/build-host.ps1` builds the production C protocol state machine with hardware-free hooks. Run `python -m unittest discover -s tools/l4s -p 'test_*.py'`. Start `python tools/l4s/simulator.py --port 4534` for a localhost TCP endpoint. Its CW progress is mocked, not a CW timing/RF simulation.

Baseline GNU Arm 15.2.Rel1 results: Fusion Flash 111824 B / RAM 12864 B; CW Flash 97892 B / RAM 11648 B. Hardware/RF/PTT acceptance and UV-K5 V3 verification remain distinct from compilation and host tests. Preserve all F4HWN/NR7Y source notices.
