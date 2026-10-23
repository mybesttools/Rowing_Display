"""Sets the display's clock (and the 5" board's RTC) to this computer's time.

Session files on the SD card are named and timestamped from this clock. The
5" board keeps it on its coin cell, so this is needed once (and again after
the cell is replaced); the 7" board has no RTC and forgets it on power-off.

    ~/.platformio/penv/bin/python scripts/set_clock.py --port /dev/cu.usbmodem21201
"""
import argparse
import time

import serial

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--port", required=True, help="the board's USB serial port")
args = ap.parse_args()

with serial.Serial(args.port, 115200, timeout=0.2) as s:
    s.reset_input_buffer()
    s.write(f"T{int(time.time())}\n".encode())
    s.flush()
    deadline = time.time() + 3
    reply = ""
    while time.time() < deadline:
        reply += s.read(4096).decode(errors="replace")
        lines = [l for l in reply.splitlines() if l.startswith("clock:")]
        if lines:
            print(lines[-1])
            break
    else:
        print("no reply from the board (is the firmware current and the port right?)")
