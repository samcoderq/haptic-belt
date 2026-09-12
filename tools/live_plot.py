"""
Live plot of the Haptic Belt mic/event-detector serial output.

Usage:
    python tools/live_plot.py [COM_PORT]

Defaults to COM20 if no port is given. Close the plot window to stop.
IMPORTANT: close any other serial monitor (VS Code terminal, PlatformIO
monitor, etc.) before running this -- only one program can hold the port.
"""

import re
import sys
import collections

import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM20"
BAUD = 115200
WINDOW = 200  # number of samples kept on screen (~20s at 10Hz)

NOTICE_THRESHOLD = 0.12
CANDIDATE_THRESHOLD = 0.25
INSTANT_THRESHOLD = 0.35

STATE_LEVELS = {"BACKGROUND": 0, "NOTICE": 1, "CANDIDATE": 2, "EVENT": 3, "COOLDOWN": 4}
STATE_COLORS = {0: "tab:green", 1: "gold", 2: "tab:orange", 3: "tab:red", 4: "tab:purple"}

rms_hist = collections.deque(maxlen=WINDOW)
baseline_hist = collections.deque(maxlen=WINDOW)
score_hist = collections.deque(maxlen=WINDOW)
state_hist = collections.deque(maxlen=WINDOW)
event_x = collections.deque(maxlen=WINDOW)
event_y = collections.deque(maxlen=WINDOW)
t = collections.deque(maxlen=WINDOW)

sample_count = 0
current_rms = None
current_baseline = None
current_state = 0
pending_score = 0.0

ser = serial.Serial(PORT, BAUD, timeout=0.2)
print(f"Connected to {PORT} at {BAUD} baud. Reading...")

fig, (ax1, ax2, ax3) = plt.subplots(
    3, 1, figsize=(9, 8), sharex=True, gridspec_kw={"height_ratios": [3, 3, 1]}
)
fig.suptitle("Haptic Belt - Mic Level, Event Score & State (live)")

line_rms, = ax1.plot([], [], label="RMS", color="tab:blue")
line_base, = ax1.plot([], [], label="Baseline", color="tab:orange", linestyle="--")
ax1.set_ylabel("Amplitude (raw units)")
ax1.legend(loc="upper left")
ax1.grid(True, alpha=0.3)

line_score, = ax2.plot([], [], label="Event Score", color="tab:red")
ax2.axhline(NOTICE_THRESHOLD, color="goldenrod", linestyle=":", label="Notice (0.12)")
ax2.axhline(CANDIDATE_THRESHOLD, color="gray", linestyle=":", label="Candidate (0.25)")
ax2.axhline(INSTANT_THRESHOLD, color="black", linestyle=":", label="Instant (0.35)")
event_scatter = ax2.scatter([], [], color="red", marker="x", s=80, label="EVENT DETECTED", zorder=5)
ax2.set_ylim(-0.05, 1.05)
ax2.set_ylabel("Score")
ax2.legend(loc="upper left")
ax2.grid(True, alpha=0.3)

line_state, = ax3.step([], [], where="post", color="black", linewidth=1.5)
ax3.set_ylim(-0.5, 4.5)
ax3.set_yticks([0, 1, 2, 3, 4])
ax3.set_yticklabels(["BACKGROUND", "NOTICE", "CANDIDATE", "EVENT", "COOLDOWN"])
ax3.set_xlabel("Sample #")
ax3.grid(True, alpha=0.3)


def update(_frame):
    global current_rms, current_baseline, current_state, sample_count, pending_score

    while ser.in_waiting:
        raw = ser.readline().decode(errors="ignore").strip()
        if not raw:
            continue

        m = re.match(r"^RMS:\s*([\d.]+)", raw)
        if m:
            current_rms = float(m.group(1))
            continue

        m = re.match(r"^Baseline:\s*([\d.]+)", raw)
        if m:
            current_baseline = float(m.group(1))
            continue

        m = re.match(r"^Event Score:\s*([\d.]+)", raw)
        if m:
            pending_score = float(m.group(1))
            continue

        m = re.match(r"^State:\s*(\w+)", raw)
        if m and current_rms is not None and current_baseline is not None:
            current_state = STATE_LEVELS.get(m.group(1), current_state)
            sample_count += 1
            t.append(sample_count)
            rms_hist.append(current_rms)
            baseline_hist.append(current_baseline)
            score_hist.append(pending_score)
            state_hist.append(current_state)
            continue

        if "EVENT DETECTED" in raw:
            if t:
                event_x.append(t[-1])
                event_y.append(score_hist[-1] if score_hist else 1.0)

    if not t:
        return line_rms, line_base, line_score, line_state, event_scatter

    line_rms.set_data(t, rms_hist)
    line_base.set_data(t, baseline_hist)
    line_score.set_data(t, score_hist)
    line_state.set_data(t, state_hist)

    if event_x:
        event_scatter.set_offsets(list(zip(event_x, event_y)))

    ax1.set_xlim(max(0, t[0]), t[-1] + 1)
    ymax = max(rms_hist + baseline_hist, default=100) * 1.1
    ax1.set_ylim(0, max(ymax, 100))

    return line_rms, line_base, line_score, line_state, event_scatter


ani = animation.FuncAnimation(fig, update, interval=100, cache_frame_data=False)
plt.tight_layout()
plt.show()

ser.close()
