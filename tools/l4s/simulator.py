"""TCP simulator backed by the production C protocol state machine. No RF access."""
import argparse
import ctypes
from pathlib import Path
import socket
import time
from protocol import Decoder, frame

ROOT = Path(__file__).resolve().parents[2]

class Firmware:
    def __init__(self, library=None):
        path = Path(library) if library else ROOT / "build/host/l4s_host.dll"
        if not path.exists():
            raise FileNotFoundError(f"Build the host library first: {path}")
        self.lib = ctypes.CDLL(str(path))
        self.lib.Host_Handle.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_void_p]
        self.lib.Host_Handle.restype = ctypes.c_size_t
        self.lib.Host_Tick.argtypes = [ctypes.c_uint32]
        self.lib.Host_Encode.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
        self.lib.Host_Encode.restype = ctypes.c_size_t
        self.lib.Host_Reset()

    def request(self, plain: bytes, now: int) -> bytes:
        output = ctypes.create_string_buffer(120)
        count = self.lib.Host_Handle(plain, len(plain), now, output)
        return output.raw[:count]

    def encode(self, plain: bytes) -> bytes:
        output = ctypes.create_string_buffer(128)
        count = self.lib.Host_Encode(plain, len(plain), output)
        return output.raw[:count]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4534)
    parser.add_argument("--library")
    args = parser.parse_args()
    firmware = Firmware(args.library)
    firmware.lib.Host_IoReset()  # Erased mock SPI storage on simulator startup; reconnect preserves it.
    epoch = time.monotonic()
    now = lambda: int((time.monotonic() - epoch) * 1000) & 0xFFFFFFFF
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((args.host, args.port))
        listener.listen(1)
        print(f"L4S simulator (no RF): {args.host}:{args.port}", flush=True)
        while True:
            client, _ = listener.accept()
            decoder = Decoder()
            with client:
                client.settimeout(0.05)
                while True:
                    firmware.lib.Host_Tick(now())
                    try:
                        data = client.recv(1024)
                    except socket.timeout:
                        continue
                    if not data:
                        firmware.lib.Host_LocalStop()
                        break
                    for request in decoder.feed(data):
                        reply = firmware.request(request, now())
                        if reply:
                            client.sendall(firmware.encode(reply))

if __name__ == "__main__":
    main()
