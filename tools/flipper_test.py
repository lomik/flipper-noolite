#!/usr/bin/env python3
"""
Hardware test helper: drives the Flipper CLI and the MTRF-64 (via noolite2mqtt).

  flipper_test.py keys "ok short" "down short" ...     send input events
  flipper_test.py log SECONDS [-- mosquitto_pub args]  capture log, optionally publishing meanwhile
  flipper_test.py cli "command"                        run one CLI command

Needs pyserial (use the fbt/ufbt toolchain python).
"""
import subprocess
import sys
import threading
import time
import glob

import serial

PROMPT = b">: "


def open_port():
    ports = glob.glob("/dev/cu.usbmodemflip_*")
    if not ports:
        sys.exit("Flipper not found")
    s = serial.Serial(ports[0], 230400, timeout=0.2)
    s.write(b"\r")
    read_until_prompt(s)
    return s


def read_until_prompt(s, timeout=3.0):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        buf += s.read(1024)
        if buf.endswith(PROMPT):
            break
    return buf.decode(errors="replace")


def cli(s, cmd):
    s.write(cmd.encode() + b"\r")
    out = read_until_prompt(s)
    return out


def main():
    mode = sys.argv[1]
    s = open_port()
    if mode == "keys":
        for k in sys.argv[2:]:
            if k.startswith("sleep"):
                time.sleep(float(k.split()[1]))
                continue
            parts = k.split()
            key = parts[0]
            kind = parts[1] if len(parts) > 1 else "short"
            # GUI ignores events without a preceding press
            seq = ["press", kind, "release"] if kind in ("short", "long") else [kind]
            if kind == "hold":  # press + long, release separately with "<key> release"
                seq = ["press", "long"]
            for t in seq:
                out = cli(s, "input send %s %s" % (key, t))
                if "Invalid" in out:
                    print(out)
            time.sleep(0.5)
    elif mode == "cli":
        print(cli(s, sys.argv[2]))
    elif mode == "log":
        seconds = float(sys.argv[2])
        pubs = []
        if "--" in sys.argv:
            rest = sys.argv[sys.argv.index("--") + 1 :]
            # groups separated by "++"; each group is one mosquitto_pub argument list
            group = []
            for a in rest + ["++"]:
                if a == "++":
                    if group:
                        pubs.append(group)
                    group = []
                else:
                    group.append(a)
        s.write(b"log info\r")
        time.sleep(0.5)

        def publish():
            time.sleep(1.0)
            for p in pubs:
                if p[0] == "sleep":
                    time.sleep(float(p[1]))
                    continue
                print("PUB", " ".join(p), flush=True)
                extra = [] if ("-m" in p or "-n" in p) else ["-n"]
                subprocess.run(["mosquitto_pub", "-h", "100.64.0.29"] + p + extra, check=False)
                time.sleep(1.5)

        t = threading.Thread(target=publish)
        t.start()
        end = time.time() + seconds
        buf = b""
        while time.time() < end:
            buf += s.read(4096)
        t.join()
        s.write(b"\x03")
        time.sleep(0.3)
        s.read(4096)
        for line in buf.decode(errors="replace").splitlines():
            if "NooLite" in line or "[E]" in line:
                print(line.strip())
    s.close()


if __name__ == "__main__":
    main()
