"""Live desktop compass window for the belt's real-time direction data.

Same data source as compass_view.py (the per-frame FRONT/RIGHT/BACK/LEFT
NORM values and DIRECTION/Confidence block main.cpp prints over serial),
but rendered as an actual Tk window instead of terminal text -- a
laptop-only stand-in for the Android app's BeltCompassDiagram.kt.

Requires stdlib tkinter + pyserial (use a Python that has both, e.g. the
system "python", not necessarily PlatformIO's venv python).

Usage: python compass_gui.py [COM_PORT]   (defaults to COM20)
"""
import math
import re
import sys
import threading
import tkinter as tk

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM20"
BAUD = 115200

MIC_RE = re.compile(r"^(FRONT|RIGHT|BACK|LEFT)\s*:\s*RMS=[\d.]+\s+PEAK=[\d.]+\s+\w+\s+NORM=([\d.]+)")
DIRECTION_RE = re.compile(r"^Direction\s*:\s*(\S+)")
CONFIDENCE_RE = re.compile(r"^Confidence\s*:\s*([\d.]+)")

# Angle (degrees, 0 = up/FRONT, clockwise) each mic sits at on the ring.
MIC_ANGLE = {"FRONT": 0, "RIGHT": 90, "BACK": 180, "LEFT": 270}

state_lock = threading.Lock()
state = {"norms": {"FRONT": 0.0, "RIGHT": 0.0, "BACK": 0.0, "LEFT": 0.0},
         "direction": "?", "confidence": 0.0, "connected": False}


def serial_reader():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
    except Exception as exc:
        with state_lock:
            state["direction"] = f"ERROR: {exc}"
        return

    with state_lock:
        state["connected"] = True

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
                with state_lock:
                    state["direction"] = m.group(1)
                continue
            m = CONFIDENCE_RE.match(line)
            if m:
                with state_lock:
                    state["confidence"] = float(m.group(1))
                awaiting_direction = False
                continue

        m = MIC_RE.match(line)
        if m:
            with state_lock:
                state["norms"][m.group(1)] = float(m.group(2))


class CompassWindow:
    SIZE = 420
    CENTER = SIZE // 2
    RING_R = 150

    def __init__(self, root):
        self.root = root
        root.title(f"Haptic Belt Compass -- {PORT}")
        self.canvas = tk.Canvas(root, width=self.SIZE, height=self.SIZE, bg="#101418", highlightthickness=0)
        self.canvas.pack()
        self.status = tk.Label(root, text="Connecting...", font=("Consolas", 12), bg="#101418", fg="#e0e0e0")
        self.status.pack(fill="x")
        root.configure(bg="#101418")
        self.redraw()

    def mic_color(self, name, active_dir, norm):
        # Base intensity ramps green->yellow->red with NORM level; active
        # side(s) in the current fused DIRECTION get a bright highlight ring.
        t = max(0.0, min(1.0, (norm - 0.8) / 1.2))
        r = int(60 + t * 195)
        g = int(200 - t * 140)
        base = f"#{r:02x}{g:02x}50"
        if name in active_dir:
            return "#ff3b3b"
        return base

    def redraw(self):
        with state_lock:
            norms = dict(state["norms"])
            direction = state["direction"]
            confidence = state["confidence"]
            connected = state["connected"]

        c = self.canvas
        c.delete("all")
        cx = cy = self.CENTER

        # outer ring
        c.create_oval(cx - self.RING_R - 20, cy - self.RING_R - 20,
                       cx + self.RING_R + 20, cy + self.RING_R + 20,
                       outline="#333a40", width=2)

        labels = {"FRONT": "FRONT", "RIGHT": "RIGHT", "BACK": "BACK", "LEFT": "LEFT"}
        for name, angle_deg in MIC_ANGLE.items():
            angle = math.radians(angle_deg - 90)  # -90 so FRONT points up
            norm = norms.get(name, 0.0)
            color = self.mic_color(name, direction, norm)

            radius = self.RING_R * min(1.15, max(0.35, norm / 1.6))
            x = cx + radius * math.cos(angle)
            y = cy + radius * math.sin(angle)

            c.create_line(cx, cy, x, y, fill=color, width=4)
            blob_r = 14 + (10 if name in direction else 0)
            c.create_oval(x - blob_r, y - blob_r, x + blob_r, y + blob_r, fill=color, outline="")

            lx = cx + (self.RING_R + 45) * math.cos(angle)
            ly = cy + (self.RING_R + 45) * math.sin(angle)
            c.create_text(lx, ly, text=f"{labels[name]}\n{norm:.2f}", fill="#e0e0e0",
                           font=("Consolas", 11, "bold"), justify="center")

        # belt body marker at center
        c.create_oval(cx - 18, cy - 18, cx + 18, cy + 18, fill="#20262c", outline="#e0e0e0", width=2)
        c.create_text(cx, cy, text="BELT", fill="#e0e0e0", font=("Consolas", 9, "bold"))

        status_text = (f"DIRECTION: {direction}   CONFIDENCE: {confidence:.2f}"
                        if connected else "Connecting to belt over serial...")
        self.status.config(text=status_text)

        self.root.after(120, self.redraw)


def main():
    t = threading.Thread(target=serial_reader, daemon=True)
    t.start()

    root = tk.Tk()
    CompassWindow(root)
    root.mainloop()


if __name__ == "__main__":
    main()
