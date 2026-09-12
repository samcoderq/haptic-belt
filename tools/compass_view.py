"""
Live dashboard for the 4-mic array: a compass (FRONT/RIGHT/BACK/LEFT nodes
around the belt) plus a text panel showing the firmware's own EVENT /
DIRECTION / PRIORITY output and a log of recent NOTICE / EVENT DETECTED /
REPEATED ACTIVITY announcements.

Node size = that mic's NORM (normalized energy) as reported by firmware.
Node color = the firmware's own DIRECTION decision (not recomputed here) --
red for a confident single direction, orange for a merged/ambiguous pair or
OMNIDIRECTIONAL, blue otherwise.

Usage:
    python tools/compass_view.py [COM_PORT]

If COM_PORT is omitted, it auto-detects the ESP32-S3's "USB Serial Device"
port. Close the window to stop. Only one program can hold the serial port
at a time -- close any other monitor/plot first.
"""

import re
import sys
import collections

import serial
import serial.tools.list_ports
import matplotlib.pyplot as plt
import matplotlib.patches as patches
import matplotlib.animation as animation


def find_port():
    candidates = [p for p in serial.tools.list_ports.comports()
                  if "USB Serial Device" in (p.description or "")]
    if not candidates:
        raise SystemExit("No 'USB Serial Device' port found -- is the board plugged in?")
    if len(candidates) > 1:
        print("Multiple matching ports found, using the first:", candidates[0].device)
    return candidates[0].device


PORT = sys.argv[1] if len(sys.argv) > 1 else find_port()
BAUD = 115200

POSITIONS = {
    "FRONT": (0, 1),
    "RIGHT": (1, 0),
    "BACK":  (0, -1),
    "LEFT":  (-1, 0),
}
NAMES = ["FRONT", "RIGHT", "BACK", "LEFT"]

MIC_LINE_RE = re.compile(
    r"^(FRONT|RIGHT|BACK|LEFT)\s*:\s*RMS=([\d.]+)\s+PEAK=([\d.]+)\s+(OK|CHECK INPUT)\s+NORM=([\d.]+)"
)
NOTICE_RE = re.compile(r"^NOTICE \| score=([\d.]+) \| direction=(\S+) \| priority=(\S+)")

SMOOTHING = 0.25  # smooths NORM for display only, direction/color come straight from firmware

# ---- live state, updated by the serial parser ----
smoothed_norm = {n: 0.0 for n in NAMES}
mic_status = {n: "OK" for n in NAMES}
pending_norm = {}

state = {
    "event_score": 0.0, "event_confidence": 0.0, "awareness_state": "BACKGROUND",
    "direction": "UNKNOWN", "direction_confidence": 0.0, "spatial": "-",
    "priority": "LOW",
}
section = None          # "event" | "direction" | "priority" | None
awaiting_priority = False
pending_event = None  # dict being filled in while parsing an EVENT DETECTED block
log = collections.deque(maxlen=6)

import time
start_time = time.time()


def log_line(text):
    log.appendleft(f"[{time.time() - start_time:6.1f}s] {text}")


ser = serial.Serial(PORT, BAUD, timeout=0.2)
print(f"Connected to {PORT} at {BAUD} baud. Reading...")

fig = plt.figure(figsize=(11, 6))
fig.suptitle("Haptic Belt - 4-Mic Spatial Dashboard (live)")
ax = fig.add_axes([0.03, 0.05, 0.55, 0.88])
info_ax = fig.add_axes([0.62, 0.05, 0.36, 0.88])
info_ax.axis("off")

ax.set_xlim(-1.8, 1.8)
ax.set_ylim(-1.8, 1.8)
ax.set_aspect("equal")
ax.axis("off")

center_dot = patches.Circle((0, 0), 0.12, color="dimgray", zorder=3)
ax.add_patch(center_dot)
ax.text(0, 0, "BELT", ha="center", va="center", fontsize=8, color="white", zorder=4)

circles = {}
rms_labels = {}
for name, (x, y) in POSITIONS.items():
    c = patches.Circle((x, y), 0.15, color="tab:blue", alpha=0.3, zorder=2)
    ax.add_patch(c)
    circles[name] = c
    ax.text(x, y + 0.35, name, ha="center", va="center", fontsize=11, fontweight="bold")
    rms_labels[name] = ax.text(x, y - 0.35, "NORM=0.00", ha="center", va="center", fontsize=9)
    ax.plot([0, x], [0, y], color="lightgray", linewidth=1, zorder=1)

state_text = info_ax.text(0, 1.0, "", va="top", ha="left", fontsize=12, family="monospace",
                           transform=info_ax.transAxes)
log_text = info_ax.text(0, 0.42, "", va="top", ha="left", fontsize=9, family="monospace",
                         transform=info_ax.transAxes, color="dimgray")
info_ax.text(0, 0.47, "Recent activity:", va="top", ha="left", fontsize=10,
             fontweight="bold", transform=info_ax.transAxes)

STATE_COLORS = {"BACKGROUND": "tab:green", "NOTICE": "gold", "CANDIDATE": "orange",
                "EVENT": "tab:red", "COOLDOWN": "tab:purple"}
PRIORITY_COLORS = {"LOW": "dimgray", "MEDIUM": "gold", "HIGH": "orange", "CRITICAL": "tab:red"}


def parse_line(raw):
    global section, awaiting_priority, pending_event

    m = MIC_LINE_RE.match(raw)
    if m:
        name, rms, peak, stat, norm = m.groups()
        pending_norm[name] = float(norm)
        mic_status[name] = stat
        if len(pending_norm) == 4:
            for n in NAMES:
                smoothed_norm[n] += (pending_norm[n] - smoothed_norm[n]) * SMOOTHING
            pending_norm.clear()
        return

    if raw == "EVENT:":
        section = "event"; return
    if raw == "DIRECTION:":
        section = "direction"; return
    if raw == "PRIORITY:":
        section = "priority"; awaiting_priority = True; return

    if awaiting_priority and raw and not raw.startswith("--") and ":" not in raw:
        state["priority"] = raw.strip()
        awaiting_priority = False
        return

    m = re.match(r"^Score\s*:\s*([\d.]+)", raw)
    if m and section == "event":
        state["event_score"] = float(m.group(1)); return

    m = re.match(r"^Confidence\s*:\s*([\d.]+)", raw)
    if m and section == "event":
        state["event_confidence"] = float(m.group(1)); return
    if m and section == "direction":
        state["direction_confidence"] = float(m.group(1)); return

    m = re.match(r"^State\s*:\s*(\w+)", raw)
    if m:
        state["awareness_state"] = m.group(1); return

    m = re.match(r"^Direction\s*:\s*(\S+)", raw)
    if m:
        state["direction"] = m.group(1)
        if pending_event is not None:
            pending_event["direction"] = m.group(1)
        return

    m = re.match(r"^Spatial\s*:\s*(\S+)", raw)
    if m:
        state["spatial"] = m.group(1); return

    m = re.match(r"^Priority\s*:\s*(\S+)", raw)  # only matches the EVENT-block "Priority : X" line
    if m and pending_event is not None:
        pending_event["priority"] = m.group(1)
        log_line(f">>> EVENT DETECTED <<< dir={pending_event.get('direction', '?')} "
                  f"pri={pending_event['priority']}")
        pending_event = None
        return

    if "EVENT DETECTED" in raw:
        pending_event = {}
        return

    m = NOTICE_RE.match(raw)
    if m:
        score, direction, priority = m.groups()
        log_line(f"NOTICE score={score} dir={direction} pri={priority}")
        return

    if "REPEATED ACTIVITY" in raw:
        log_line(f">>> REPEATED ACTIVITY <<< dir={state['direction']}")
        return


def node_color_alpha(name):
    d = state["direction"]
    conf = state["direction_confidence"]
    if d == name:
        return "tab:red", 0.4 + 0.6 * conf
    if d == "OMNIDIRECTIONAL":
        return "orange", 0.4 + 0.4 * conf
    if "-" in d and name in d.split("-"):
        return "orange", 0.4 + 0.5 * conf
    return "tab:blue", 0.3


def update(_frame):
    while ser.in_waiting:
        raw = ser.readline().decode(errors="ignore").strip()
        if raw:
            parse_line(raw)

    maxNorm = max(max(smoothed_norm.values(), default=1.0), 1.0)
    for name in NAMES:
        norm = smoothed_norm[name]
        c = circles[name]
        c.set_radius(0.15 + 0.35 * min(norm / maxNorm, 1.0))
        color, alpha = node_color_alpha(name)
        c.set_color(color)
        c.set_alpha(alpha)
        c.set_edgecolor("red" if mic_status[name] != "OK" else "none")
        c.set_linewidth(3 if mic_status[name] != "OK" else 0)
        rms_labels[name].set_text(f"NORM={norm:.2f}")

    awareness = state["awareness_state"]
    priority = state["priority"]
    summary = (
        f"State      : {awareness}\n"
        f"Event score: {state['event_score']:.2f}\n"
        f"Event conf : {state['event_confidence']:.2f}\n"
        f"\n"
        f"Direction  : {state['direction']}\n"
        f"Dir. conf. : {state['direction_confidence']:.2f}\n"
        f"Spatial    : {state['spatial']}\n"
        f"\n"
        f"Priority   : {priority}"
    )
    state_text.set_text(summary)
    state_text.set_color(STATE_COLORS.get(awareness, "black"))

    log_text.set_text("\n".join(log))

    return list(circles.values()) + [state_text, log_text]


ani = animation.FuncAnimation(fig, update, interval=100, cache_frame_data=False)
plt.show()

ser.close()
