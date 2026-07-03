#!/usr/bin/env python3
"""Convert a dumped Playdate framebuffer (frame.bin) to PNG.

Layout: 240 rows of 52 bytes, 400 px used per row, MSB-first, 1 = white.
Written without external deps (PNG encoded by hand, zlib stdlib only).
"""
import struct
import sys
import zlib

ROWSIZE = 52
W, H = 400, 240


def main(src: str, dst: str, scale: int = 1) -> None:
    data = open(src, "rb").read()
    assert len(data) >= H * ROWSIZE, f"short file: {len(data)}"

    rows = []
    for y in range(H):
        row = bytearray()
        base = y * ROWSIZE
        for x in range(W):
            byte = data[base + (x >> 3)]
            bit = (byte >> (7 - (x & 7))) & 1
            row += bytes((255 if bit else 0,)) * scale
        for _ in range(scale):
            rows.append(b"\x00" + bytes(row))

    raw = zlib.compress(b"".join(rows), 6)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", W * scale, H * scale, 8, 0, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", raw) + chunk(b"IEND", b""))
    open(dst, "wb").write(png)
    print(f"wrote {dst}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1)
