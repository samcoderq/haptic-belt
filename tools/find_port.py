"""Prints the COM port of the ESP32-S3's USB Serial Device, so you don't
have to check Device Manager every time it re-enumerates after a flash."""
import serial.tools.list_ports

candidates = [p for p in serial.tools.list_ports.comports() if "USB Serial Device" in (p.description or "")]

if not candidates:
    print("No 'USB Serial Device' port found. Is the board plugged in?")
elif len(candidates) == 1:
    print(candidates[0].device)
else:
    for p in candidates:
        print(f"{p.device}  ({p.description})")
