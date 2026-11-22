#!/usr/bin/env python3
"""Eenvoudige STL-renderer (alleen Pillow nodig) voor voorbeeldplaatjes van de behuizing.

  render.py uit.png onderdeel.stl[:kleur] ... [--view front|back|side] [--tilt 15]
"""
import math
import struct
import sys

from PIL import Image, ImageDraw

COLORS = {"wit": (235, 235, 230), "grijs": (90, 95, 105), "blauw": (70, 110, 170),
          "groen": (60, 140, 90), "oranje": (220, 130, 50)}


def load_stl(path):
    data = open(path, "rb").read()
    n = struct.unpack_from("<I", data, 80)[0]
    tris = []
    for i in range(n):
        v = struct.unpack_from("<12f", data, 84 + i * 50)
        tris.append((v[3:6], v[6:9], v[9:12]))
    return tris


def rot(p, yaw, pitch, tilt):
    x, y, z = p
    # kantelen om de x-as (lijstje achterover): model-z is naar achteren
    t = math.radians(tilt)
    y, z = y * math.cos(t) - z * math.sin(t), y * math.sin(t) + z * math.cos(t)
    # wereld: x rechts, y omhoog, z van de kijker af
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    x, z = x * c + z * s, -x * s + z * c
    c, s = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    y, z = y * c - z * s, y * s + z * c
    return x, y, z


def render(out, parts, view="front", tilt=15.0, size=(900, 900)):
    yaw, pitch = {"front": (-30, 20), "back": (150, 20), "side": (-90, 0)}[view]
    faces = []
    light = (0.4, 0.7, -0.6)
    ln = math.sqrt(sum(c * c for c in light))
    light = tuple(c / ln for c in light)
    for path, color in parts:
        for tri in load_stl(path):
            p = [rot(v, yaw, pitch, tilt) for v in tri]
            ax, ay, az = (p[1][i] - p[0][i] for i in range(3))
            bx, by, bz = (p[2][i] - p[0][i] for i in range(3))
            nx, ny, nz = ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx
            nl = math.sqrt(nx * nx + ny * ny + nz * nz) or 1
            nx, ny, nz = nx / nl, ny / nl, nz / nl
            if nz > 0:          # achterkant van het vlak: niet tekenen
                continue
            shade = 0.35 + 0.65 * max(0.0, nx * light[0] + ny * light[1] + nz * light[2])
            col = tuple(int(c * shade) for c in color)
            faces.append((sum(q[2] for q in p) / 3, p, col))
    xs = [q[0] for _, p, _ in faces for q in p]
    ys = [q[1] for _, p, _ in faces for q in p]
    scale = 0.85 * min(size[0] / (max(xs) - min(xs)), size[1] / (max(ys) - min(ys)))
    ox = size[0] / 2 - scale * (max(xs) + min(xs)) / 2
    oy = size[1] / 2 + scale * (max(ys) + min(ys)) / 2
    img = Image.new("RGB", size, (250, 250, 252))
    d = ImageDraw.Draw(img)
    for _, p, col in sorted(faces, key=lambda f: -f[0]):
        d.polygon([(ox + scale * q[0], oy - scale * q[1]) for q in p], fill=col)
    if view == "side":   # tafelvlak
        ymin = min(ys)
        d.line([(0, oy - scale * ymin), (size[0], oy - scale * ymin)], fill=(150, 80, 80), width=2)
    img.save(out)


if __name__ == "__main__":
    args = sys.argv[1:]
    view = args[args.index("--view") + 1] if "--view" in args else "front"
    tilt = float(args[args.index("--tilt") + 1]) if "--tilt" in args else 15.0
    files = [a for a in args[1:] if a.endswith(".stl") or ".stl:" in a]
    parts = []
    for f in files:
        path, _, col = f.partition(":")
        parts.append((path, COLORS.get(col, COLORS["wit"])))
    render(args[0], parts, view, tilt)
