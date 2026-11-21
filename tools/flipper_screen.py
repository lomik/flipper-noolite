#!/usr/bin/env python3
"""Grab one Flipper screen frame over RPC and save it as PNG: flipper_screen.py out.png [scale]"""
import glob
import struct
import sys
import time
import zlib

import serial


def varint(n):
    out = b""
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out += bytes([b | 0x80])
        else:
            return out + bytes([b])


def read_varint(buf, pos):
    shift = result = 0
    while True:
        if pos >= len(buf):
            return None, pos
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, pos
        shift += 7


def main_msg(cmd_id, field, payload=b""):
    body = b"\x08" + varint(cmd_id) + varint((field << 3) | 2) + varint(len(payload)) + payload
    return varint(len(body)) + body


def find_field(msg, field):
    pos = 0
    while pos < len(msg):
        tag, pos = read_varint(msg, pos)
        if tag is None:
            return None
        f, wt = tag >> 3, tag & 7
        if wt == 0:
            _, pos = read_varint(msg, pos)
        elif wt == 2:
            ln, pos = read_varint(msg, pos)
            data = msg[pos : pos + ln]
            pos += ln
            if f == field:
                return data
        else:
            return None
    return None


def save_png(frame, path, scale):
    w, h = 128 * scale, 64 * scale
    raw = b""
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            px, py = x // scale, y // scale
            on = frame[(py // 8) * 128 + px] >> (py % 8) & 1
            row += bytes((0x10, 0x10, 0x10) if on else (0xFF, 0x8C, 0x29))
        raw += bytes(row)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )
    open(path, "wb").write(png)


def main():
    out = sys.argv[1]
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    s = serial.Serial(glob.glob("/dev/cu.usbmodemflip_*")[0], 230400, timeout=0.3)
    s.write(b"\r")
    time.sleep(0.3)
    s.read(4096)
    s.write(b"start_rpc_session\r")
    time.sleep(0.3)
    s.read(4096)
    s.write(main_msg(1, 20))
    buf = b""
    frame = None
    end = time.time() + 5
    while time.time() < end and frame is None:
        buf += s.read(4096)
        pos = 0
        while True:
            ln, p2 = read_varint(buf, pos)
            if ln is None or p2 + ln > len(buf):
                break
            msg = buf[p2 : p2 + ln]
            pos = p2 + ln
            sf = find_field(msg, 22)
            if sf is not None:
                data = find_field(sf, 1)
                if data and len(data) == 1024:
                    frame = data
                    break
        buf = buf[pos:]
    s.write(main_msg(2, 21))
    time.sleep(0.2)
    s.write(main_msg(3, 19))
    time.sleep(0.3)
    s.close()
    if frame is None:
        sys.exit("no frame")
    save_png(frame, out, scale)
    print(out)


if __name__ == "__main__":
    main()
