"""Live terminal compass view of the belt's real-time direction data.

Reads the per-frame summary block main.cpp already prints over serial
(FRONT/RIGHT/BACK/LEFT NORM values, then a DIRECTION: section with
Direction/Confidence) and redraws an ASCII compass rose in place -- a
laptop-only stand-in for the Android app's BeltCompassDiagram.kt, useful
for checking direction behavior without installing/opening the app.

Usage: python compass_view.py [COM_PORT]   (defaults to COM20)
"""
import re
import sys

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM20"
BAUD = 115200

MIC_RE = re.compile(r"^(FRONT|RIGHT|BACK|LEFT)\s*:\s*RMS=[\d.]+\s+PEAK=[\d.]+\s+\w+\s+NORM=([\d.]+)")
DIRECTION_RE = re.compile(r"^Direction\s*:\s*(\S+)")
CONFIDENCE_RE = re.compile(r"^Confidence\s*:\s*([\d.]+)")


def bar(value, width=16, scale=2.0):
    n = max(0, min(width, int(value / scale * width)))
    return "#" * n + "." * (width - n)


def render(norms, direction, confidence):
    active = direction or ""
    def mark(name):
        return "  <== SOUND" if name in active else ""

    print("\033c", end="")  # ANSI clear
    print("=" * 56)
    print("   BELT COMPASS -- live, real mic data (Ctrl+C to quit)")
    print("=" * 56)
    print(f"                 FRONT {norms.get('FRONT', 0):.2f}{mark('FRONT')}")
    print(f"                 {bar(norms.get('FRONT', 0))}")
    print()
    print(f"LEFT {norms.get('LEFT', 0):.2f}{mark('LEFT'):<14}"
          + " " * 6
          + f"RIGHT {norms.get('RIGHT', 0):.2f}{mark('RIGHT')}")
    print(f"{bar(norms.get('LEFT', 0), 14)}"
          + " " * 10
          + f"{bar(norms.get('RIGHT', 0), 14)}")
    print()
    print(f"                 {bar(norms.get('BACK', 0))}")
    print(f"                 BACK {norms.get('BACK', 0):.2f}{mark('BACK')}")
    print("=" * 56)
    print(f"DIRECTION: {direction}    CONFIDENCE: {confidence}")
    print("=" * 56)


def main():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    norms = {}
    direction = "?"
    confidence = "?"
    awaiting_direction = False

    while True:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode(errors="replace").strip()

        if line == "DIRECTION:":
            awaiting_direction = True
            continue

        if awaiting_direction:
            m = DIRECTION_RE.match(line)
            if m:
                direction = m.group(1)
                continue
            m = CONFIDENCE_RE.match(line)
            if m:
                confidence = m.group(1)
                awaiting_direction = False
                render(norms, direction, confidence)
                continue

        m = MIC_RE.match(line)
        if m:
            norms[m.group(1)] = float(m.group(2))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
