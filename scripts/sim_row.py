"""Simulated rowing over USB serial, for testing without a rower.

Starts a session, sends wheel-turn pulses shaped like real strokes (wheel
speeding up on the drive, slowing on the recovery) and ends the session, so
the session file, averages and FTMS output can be checked at a desk. Needs
"Each pulse is: a wheel turn" in Settings (the default).

    ~/.platformio/penv/bin/python scripts/sim_row.py --port /dev/cu.usbmodem21201 --strokes 20
"""
import argparse
import time

import serial

# One stroke at ~25 spm: intervals between wheel turns, in seconds.
DRIVE = [0.34, 0.26, 0.21, 0.19]
RECOVERY = [0.22, 0.26, 0.31, 0.37, 0.44]

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--port", required=True)
ap.add_argument("--strokes", type=int, default=20)
ap.add_argument("--no-end", action="store_true", help="leave the session running")
args = ap.parse_args()

with serial.Serial(args.port, 115200, timeout=0) as s:
    s.write(b"S\n")
    time.sleep(1.0)
    for stroke in range(args.strokes):
        for dt in DRIVE + RECOVERY:
            time.sleep(dt)
            s.write(b"P\n")
        s.reset_input_buffer()
        print(f"stroke {stroke + 1}/{args.strokes}", flush=True)
    if not args.no_end:
        time.sleep(1.0)
        s.write(b"E\n")
        out = ""
        deadline = time.time() + 8
        while time.time() < deadline and "session: saved" not in out and "writing failed" not in out:
            out += s.read(4096).decode(errors="replace")
            time.sleep(0.1)
        for line in out.splitlines():
            if line.startswith("session:") or line.startswith("sd:"):
                print(line)
