#!/usr/bin/env python3
"""Pull the on-device pulse log (firmware/src/pulse_log.cpp) off the board.

Reads the "spiffs" partition with esptool, saves the text log under logs/,
and prints a per-boot summary. --clear wipes the log afterwards (the
firmware reformats the partition on its next boot).

    ~/.platformio/penv/bin/python scripts/fetch_pulse_log.py --port /dev/cu.usbmodem21201 [--clear]

Line format:
    B,ms                      boot
    C,ms,machine=..,sensor=..,...  settings applied (boot and every save)
    R,ms                      session reset (start button)
    E,ms,level                DI edge seen by the poller (0 = contact closed)
    P,ms                      pulse handed to the metrics model
    S,ms,reads,lockFails,i2cFails,maxGapMs   poll coverage over the last second
    M,ms,rpm,spm,watts,dist,strokes,phase,drag   metrics snapshot (phase 0/1/2 = idle/drive/recovery)
"""

import argparse
import datetime
import pathlib
import subprocess
import sys

PART_OFFSET = 0x670000
PART_SIZE = 0x180000
CHUNK = 0x10000
HEADER = b"ROWLOG1\n"
ESPTOOL = pathlib.Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py"
ROOT = pathlib.Path(__file__).resolve().parent.parent


def esptool(port, *args):
    subprocess.run([sys.executable, str(ESPTOOL), "--chip", "esp32s3", "--port", port, *args], check=True)


def summarize(text):
    boots = []
    for line in text.splitlines():
        f = line.split(",")
        if f[0] == "B":
            boots.append({"boot": line, "E": 0, "P": [], "S": [], "M": []})
        elif boots:
            b = boots[-1]
            if f[0] == "E":
                b["E"] += 1
            elif f[0] == "P":
                b["P"].append(int(f[1]))
            elif f[0] == "S":
                b["S"].append([int(x) for x in f[1:]])
            elif f[0] == "M":
                b["M"].append(f)

    for i, b in enumerate(boots):
        secs = len(b["S"])
        if secs == 0:
            continue
        reads = sum(s[1] for s in b["S"])
        lock = sum(s[2] for s in b["S"])
        i2c = sum(s[3] for s in b["S"])
        max_gap = max(s[4] for s in b["S"])
        p = b["P"]
        gaps = [b2 - a for a, b2 in zip(p, p[1:])]
        last = b["M"][-1] if b["M"] else None
        print(f"--- boot {i}: {b['boot']}  ({secs} s logged)")
        print(f"  polls: {reads} ok, {lock} lock-timeouts, {i2c} i2c-fails, worst gap between reads {max_gap} ms")
        print(f"  edges: {b['E']}, pulses to model: {len(p)}")
        if gaps:
            gaps_sorted = sorted(gaps)
            print(f"  pulse interval ms: min {gaps_sorted[0]}, median {gaps_sorted[len(gaps)//2]}, max {gaps_sorted[-1]}")
        if last:
            print(f"  last metrics: rpm={last[2]} spm={last[3]} watts={last[4]} dist={last[5]} strokes={last[6]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--clear", action="store_true", help="wipe the log after fetching")
    args = ap.parse_args()

    out_dir = ROOT / "logs"
    out_dir.mkdir(exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    raw_path = out_dir / f"pulse_log_{stamp}.bin"

    # Read in chunks and stop at the first erased byte; a full-partition read
    # takes minutes over USB.
    raw = b""
    while len(raw) < PART_SIZE:
        n = min(CHUNK, PART_SIZE - len(raw))
        esptool(args.port, "--after", "no_reset", "read_flash", hex(PART_OFFSET + len(raw)), hex(n), str(raw_path))
        chunk = raw_path.read_bytes()
        raw += chunk
        if b"\xff" in chunk:
            break
    raw_path.unlink()
    esptool(args.port, "--before", "no_reset", "run")
    if not raw.startswith(HEADER):
        sys.exit("No log found (partition not formatted yet).")
    body = raw[len(HEADER):]
    end = body.find(b"\xff")
    text = body[: end if end >= 0 else len(body)].decode(errors="replace")

    txt_path = out_dir / f"pulse_log_{stamp}.csv"
    txt_path.write_text(text)
    print(f"Saved {len(text)} bytes to {txt_path}")
    summarize(text)

    if args.clear:
        # Invalidating the header is enough: the firmware erases the whole
        # partition on its next boot when the header is missing.
        esptool(args.port, "erase_region", hex(PART_OFFSET), "0x1000")
        print("Log cleared.")


if __name__ == "__main__":
    main()
