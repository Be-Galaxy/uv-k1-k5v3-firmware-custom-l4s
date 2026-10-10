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

Modes: FM=0, AM=1, USB receive=2, CW=3. TX accepts FM/CW only. PTT-ON is for FM voice; CW uses its own firmware playback engine. Readiness requires PAIR and MODE to have been applied. After STOP/PTT-OFF, apply a newer PAIR revision before requesting another transmission.

Results: APPLIED=0, ACCEPTED=1, BAD_LENGTH=2, BAD_SESSION=3, BUSY=4, RANGE=5, UNSUPPORTED=6, TX_REJECTED=7, CANCELLED=8, STALE=9, NOT_READY=10, TIMEOUT=11. ACCEPTED is not proof of an applied frequency. Poll the applied revision before keying. During TX, only the newest pending pair is kept; the default build applies it after TX ends. STOP cancels pending work.

## Replies

HELLO data (42 bytes): `L4S1` (4), family:u8=3, features:u32, resolution Hz:u16=10, max text:u8=92, minimum WPM:u8=10, maximum WPM:u8=45, watchdog ms:u16=1500, RX envelope minimum Hz:u32, maximum Hz:u32, firmware label:16 zero-padded ASCII bytes, RX mode mask:u8, TX mode mask:u8. Family 3 denotes the shared UV-K1/K5 V3 firmware, not an individually detected model. The RX envelope includes gaps; every frequency is still validated by the firmware. TX is governed by the existing radio frequency lock and per-channel TX lock.

Feature bits: atomic pair=1, PTT=2, CW text=4, CTCSS=8, live FM TX retune=16, CW-gap retune=32. Default builds do not advertise the last two before hardware validation.

Other replies contain a 24-byte snapshot: RX Hz:u32, TX Hz:u32, applied revision:u32, pending revision:u32, RX mode:u8, TX mode:u8, TX enabled:u8, TX state:u8, CW state:u8, CW character position:u8, CW total:u8, last error:u8. TX state: OFF=0, remote voice=1, CW operation=2, local TX=3. CW state: IDLE=0, QUEUED=1, SENDING=2, DONE=3, CANCELLED=4, FAILED=5. A CW operation includes its key-up gaps; it does not mean the carrier is continuously on.

Frequency values mean intended RF frequency, not a CW BFO/sidetone offset. The firmware rounds to the nearest 10 Hz and applies its existing receive BFO once. Remote sessions temporarily disable CW cross-mode offset. Software state/readback is not a measured RF frequency.

## Operation and recovery

1. Build with `cmake --preset L4S` and `cmake --build --preset L4S`. Output: `build/L4S/l4s.control.bin`, `.uf2`, `.hex`, `.elf`, `.map`.
2. Back up calibration/configuration with the existing UV Studio procedure before any flashing. This development work does not change calibration or bootloader layout; UF2 application base stays `0x08002800`.
3. Select a non-USB CW key-input mode on the radio before connecting USB CDC: the USB paddle/handkey modes use the same PA11/PA12 pins. Use an OTG-capable Android phone and a data cable, not a charging-only cable.
4. HELLO, OPEN, MODE, PAIR, then KEEPALIVE every 500 ms. Confirm the applied revision before PTT/CW. The radio must be on its main screen and idle to open a session. Existing lock, battery, busy-channel and TOT checks remain applicable.
5. Send STOP on button release, touch cancellation, application backgrounding, pass end or error; CLOSE before normal disconnect. Reconnect with a new session and reconfigure. No transmission is automatically resumed.

A 1500 ms watchdog stops remote TX/CW and restores the original VFO, scan/dual-watch, VOX, battery-save and CW settings. DTR loss also requests shutdown. Local keys abort a session; a physically held PTT must be released before another local transmission. Tracking commands never save VFO/channel/settings to Flash.

Restore the baseline firmware using the same UV Studio/UF2 procedure and the saved configuration/calibration if rollback is needed. Do not restore incompatible memory layouts without the original firmware's migration procedure. Default application image size must stay within 118 KiB; the linker enforces the 16 KiB RAM region including reserved stack/heap.

## Host verification

`tools/l4s/build-host.ps1` builds the production C protocol state machine with hardware-free hooks. Run `python -m unittest discover -s tools/l4s -p 'test_*.py'`. Start `python tools/l4s/simulator.py --port 4534` for a localhost TCP endpoint. Its CW progress is mocked, not a CW timing/RF simulation.

Baseline GNU Arm 15.2.Rel1 results: Fusion Flash 111824 B / RAM 12864 B; CW Flash 97892 B / RAM 11648 B. Hardware/RF/PTT acceptance and UV-K5 V3 verification remain distinct from compilation and host tests. Preserve all F4HWN/NR7Y source notices.
