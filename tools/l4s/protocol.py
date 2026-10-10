"""L4S v1 wire codec. No serial dependency is required for the simulator."""
import struct
from dataclasses import dataclass

MASK = bytes.fromhex("166c14e62e910d402135d5401303e980")
HELLO, OPEN, KEEPALIVE, CLOSE, STATUS, PAIR, MODE, TONE, PTT, CW, STOP = range(0x700, 0x70B)
MAX_BODY = 120

def crc(data: bytes) -> int:
    result = 0
    for value in data:
        result ^= value << 8
        for _ in range(8):
            result = ((result << 1) ^ (0x1021 if result & 0x8000 else 0)) & 0xFFFF
    return result

def body(command: int, sequence: int, session: int, data: bytes = b"") -> bytes:
    return struct.pack("<HHBBHI", command, len(data) + 8, 1, 0, sequence, session) + data

def frame(plain: bytes) -> bytes:
    if not 12 <= len(plain) <= MAX_BODY:
        raise ValueError("Invalid body length")
    encrypted = plain + struct.pack("<H", crc(plain))
    encrypted = bytes(value ^ MASK[i % 16] for i, value in enumerate(encrypted))
    return b"\xab\xcd" + struct.pack("<H", len(plain)) + encrypted + b"\xdc\xba"

class Decoder:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[bytes]:
        self.buffer.extend(data)
        if len(self.buffer) > 4096:
            del self.buffer[:-4096]
        replies = []
        while len(self.buffer) >= 4:
            if self.buffer[:2] != b"\xab\xcd":
                del self.buffer[0]
                continue
            size = struct.unpack_from("<H", self.buffer, 2)[0]
            if not 12 <= size <= MAX_BODY:
                del self.buffer[0]
                continue
            if len(self.buffer) < size + 8:
                break
            encrypted = self.buffer[4:size + 6]
            plain = bytes(value ^ MASK[i % 16] for i, value in enumerate(encrypted))
            if self.buffer[size + 6:size + 8] != b"\xdc\xba" or crc(plain[:-2]) != struct.unpack_from("<H", plain, size)[0]:
                del self.buffer[0]
                continue
            replies.append(plain[:-2])
            del self.buffer[:size + 8]
        return replies

@dataclass(frozen=True)
class Snapshot:
    rx_hz: int
    tx_hz: int
    revision: int
    pending_revision: int
    rx_mode: int
    tx_mode: int
    tx_enabled: int
    tx_state: int
    cw_state: int
    cw_position: int
    cw_length: int
    error: int

    @classmethod
    def parse(cls, reply: bytes):
        return cls(*struct.unpack_from("<IIIIBBBBBBBB", reply, 12))
