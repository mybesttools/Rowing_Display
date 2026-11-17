#!/usr/bin/env python3
"""Pull the on-device pulse log (firmware/src/pulse_log.cpp) off the board.

Reads the "spiffs" partition with esptool, puts the logs back together from
its 4 KB sectors (the firmware keeps the newest 7, one per boot that saw
rowing), saves them oldest first under logs/ and prints a per-boot summary.
--clear wipes the partition afterwards.

    ~/.platformio/penv/bin/python scripts/fetch_pulse_log.py --port /dev/cu.usbmodem21201 [--clear]

Line format:
    B,ms                      boot
    C,ms,machine=..,sensor=..,...  settings applied (boot and every save)
    R,ms                      session reset (start button)
    E,ms,level                DI edge seen by the poller (0 = contact closed)
    P,ms                      pulse handed to the metrics model
    S,ms,reads,lockFails,i2cFails,maxGapMs   poll coverage over the last second
    M,ms,rpm,spm,watts,dist,strokes,phase,drag   metrics snapshot (phase 0/1/2 = idle/drive/recovery)
    D,ms,count                lines dropped while rowing (buffer full; E/S/M go first)
"""

import argparse
import datetime
import pathlib
import struct
import subprocess
import sys
import time

PART_OFFSET = 0x670000
PART_SIZE = 0x180000
CHUNK = 0x10000
SECTOR = 0x1000
MAGIC = b"RLG2"
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

    # The logs can sit anywhere in the ring, so the whole partition is read,
    # in chunks: one long read over USB can drop out part way.
    raw = b""
    while len(raw) < PART_SIZE:
        for attempt in range(3):
            try:
                esptool(args.port, "--after", "no_reset", "read_flash", hex(PART_OFFSET + len(raw)), hex(CHUNK),
                        str(raw_path))
                break
            except subprocess.CalledProcessError:
                if attempt == 2:
                    raise
                time.sleep(2)
        raw += raw_path.read_bytes()
    raw_path.unlink()
    esptool(args.port, "--before", "no_reset", "run")

    sectors = []
    for off in range(0, len(raw), SECTOR):
        if raw[off : off + 4] == MAGIC:
            seq, index = struct.unpack_from("<II", raw, off + 4)
            sectors.append((seq, index, raw[off + 12 : off + SECTOR].rstrip(b"\xff")))
    if not sectors:
        sys.exit("No log found.")
    sectors.sort()
    seqs = sorted({s[0] for s in sectors})
    print(f"{len(seqs)} log(s) on the device: #{seqs[0]} to #{seqs[-1]}")
    text = b"".join(s[2] for s in sectors).decode(errors="replace")

    txt_path = out_dir / f"pulse_log_{stamp}.csv"
    txt_path.write_text(text)
    print(f"Saved {len(text)} bytes to {txt_path}")
    summarize(text)

    if args.clear:
        esptool(args.port, "erase_region", hex(PART_OFFSET), hex(PART_SIZE))
        print("Log cleared.")


if __name__ == "__main__":
    main()
