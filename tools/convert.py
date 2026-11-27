#!/usr/bin/env python3
"""Zet een foto om naar het 7-kleuren ACeP-formaat (600x448, 4 bit/pixel).

Gebruik:
  convert.py foto.jpg [--preview uit.png] [--send /dev/ttyACM0]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "server"))
from epaper import FRAME_BYTES, H, W, convert, pack  # noqa: E402


def send(port, data, temp_offset=0, pll=0x3C, passes=1, external_sensor=False, cdi=0x37):
    import serial

    # Gebruik de native USB-poort van de fotolijst. Openen van de COM-poort (USB-serieel
    # chip) reset het bordje, en die poort is te traag voor een heel beeld.
    with serial.Serial(port, 115200, timeout=1) as s:
        s.reset_input_buffer()
        tse = (0x80 if external_sensor else 0) | (temp_offset & 0x0F)
        s.write(b"IMG2" + bytes([tse, pll, passes, cdi]))
        for i in range(0, len(data), 4096):
            s.write(data[i:i + 4096])
        s.flush()
        t = time.time()
        received = False   # regels van een eerdere (WiFi-)verversing negeren
        while time.time() - t < 120 * passes + 60:
            line = s.readline().decode(errors="replace").strip()
            if line:
                print("  lijst:", line)
            if line.startswith("OK ontvangen") or line.startswith("ERR timeout"):
                received = True
            if received and line.startswith(("DONE", "ERR")):
                return line.startswith("DONE")
    print("  geen antwoord van de fotolijst")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--preview")
    ap.add_argument("--send")
    ap.add_argument("--clear", action="store_true",
                    help="maak het scherm eerst wit (zoals de Waveshare-demo)")
    ap.add_argument("--temp-offset", type=int, default=0,
                    help="-8..7: laat het paneel een koudere/warmere LUT kiezen")
    ap.add_argument("--pll", type=lambda v: int(v, 0), default=0x3C,
                    help="frame rate, 0x3C = 50 Hz (standaard)")
    ap.add_argument("--external-sensor", action="store_true",
                    help="laat de chip een (niet aanwezige) externe sensor lezen")
    ap.add_argument("--passes", type=int, default=1, help="aantal keer verversen (1-3)")
    ap.add_argument("--saturation", type=float, default=1.5)
    ap.add_argument("--contrast", type=float, default=1.2)
    ap.add_argument("--warmth", type=float, default=0.0,
                    help="trekt het beeld naar rood (0 = neutraal)")
    a = ap.parse_args()

    q = convert(a.image, saturation=a.saturation, contrast=a.contrast, warmth=a.warmth)
    if a.preview:
        q.convert("RGB").save(a.preview)
        print("preview:", a.preview)
    if a.send:
        if a.clear:
            print("scherm wit maken...")
            if not send(a.send, bytes([0x11]) * (W * H // 2)):
                sys.exit(1)
        ok = send(a.send, pack(q), a.temp_offset, a.pll, a.passes, a.external_sensor)
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
