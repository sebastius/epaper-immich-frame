# Parametrische behuizing voor de e-paper fotolijst (staand/portret, 15 graden achterover).
#
# Onderdelen (STL in case/stl/):
#   front.stl   voorkant met venster en wanden; print met de voorkant op het bed
#   back.stl    deksel met drukblokjes voor het display en paaltjes voor de Pico W;
#               print met de buitenkant op het bed
#   stand.stl   standaard met twee vinnen; print met de grondplaat op het bed
#
# Draaien:  freecad.cmd -c "exec(open('case/fotolijst_case.py').read())"
#
# Assenstelsel (portret, van voren gezien):
#   x = breedte (links -> rechts), y = hoogte (onder -> boven), z = diepte (voorkant = 0,
#   naar achteren positief).
#
# Schroeven: 4x M2.5x10 (deksel), 2x M2.5x8 (standaard), 4x M2x5 (Pico W), allemaal
# zelftappend in de voorgeboorde gaten.

import math
import os

import FreeCAD as App
import MeshPart
import Part

V = App.Vector
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else
                   "/home/bas/fotokijken/case", "stl")

# ------------------------------------------------------------------ parameters (mm)
# Waveshare 5.65" e-Paper Module (F), portret. CONTROLEER MET EEN SCHUIFMAAT.
PCB_W, PCB_H = 100.5, 138.5      # printplaat (incl. scherm)
MODULE_T = 3.2                    # voorkant glas tot achterkant printplaat
ACTIVE_W, ACTIVE_H = 85.8, 114.9  # zichtbaar beeldvlak
ACTIVE_X0 = 5.5                   # afstand linkerrand printplaat -> beeldvlak
ACTIVE_Y0 = 11.8                  # afstand onderrand printplaat -> beeldvlak
WINDOW_MARGIN = 1.0               # venster rondom groter dan beeldvlak

CLEAR = 0.4          # speling rondom het display in de kast
WALL = 4.5           # wanddikte (ruimte voor de hoekschroeven)
FRONT_T = 2.0        # dikte voorplaat
CHAMFER = 1.5        # afschuining van het venster aan de voorkant
DEPTH = 26.0         # totale diepte kast incl. deksel
LID_T = 2.5          # dikte deksel

TILT = 15.0          # graden achterover

# Raspberry Pi Pico W
PICO_W, PICO_L, PICO_T = 21.0, 51.0, 1.0
PICO_HOLES = [(4.8, 2.0), (16.2, 2.0), (4.8, 49.0), (16.2, 49.0)]  # (x, y) vanaf hoek, USB-kant y=0
PICO_POST_H = 4.0
USB_W, USB_H = 12.0, 9.0          # opening voor micro-USB-stekker

# standaard
STAND_LEN = 55.0     # hoe ver de voet achter de kast uitsteekt
STAND_TOP = 95.0     # hoogte waarop de vinnen aan de achterkant eindigen
STAND_PLATE_T = 4.0
FIN_T = 5.0
FIN_DX = 25.0        # vinnen op +/- FIN_DX van het midden

PILOT_25, CLEAR_25 = 2.1, 2.9     # M2.5: voorboring / doorgaand gat
PILOT_2 = 1.6                     # M2: voorboring

# ------------------------------------------------------------------ afgeleide maten
IN_W, IN_H = PCB_W + 2 * CLEAR, PCB_H + 2 * CLEAR
OUT_W, OUT_H = IN_W + 2 * WALL, IN_H + 2 * WALL
PCB_X, PCB_Y = WALL + CLEAR, WALL + CLEAR          # linksonder van de printplaat
MODULE_BACK = FRONT_T + MODULE_T                    # z van achterkant printplaat
LID_IN = DEPTH - LID_T                              # z van binnenkant deksel
CX = OUT_W / 2
TAN = math.tan(math.radians(TILT))
CORNERS = [(WALL / 2, WALL / 2), (OUT_W - WALL / 2, WALL / 2),
           (WALL / 2, OUT_H - WALL / 2), (OUT_W - WALL / 2, OUT_H - WALL / 2)]


def box(x, y, z, w, h, d):
    return Part.makeBox(w, h, d, V(x, y, z))


def cyl(x, y, z, r, d):
    return Part.makeCylinder(r, d, V(x, y, z))


def yz_prism(points, x0, width):
    """Prisma met doorsnede `points` (lijst (y, z)) in het yz-vlak, van x0 tot x0+width."""
    wire = Part.makePolygon([V(x0, y, z) for y, z in points] + [V(x0, *points[0])])
    return Part.Face(wire).extrude(V(width, 0, 0))


# ------------------------------------------------------------------ voorkant
def make_front():
    shell = box(0, 0, 0, OUT_W, OUT_H, LID_IN)
    shell = shell.cut(box(WALL, WALL, FRONT_T, IN_W, IN_H, LID_IN))

    # venster, aan de voorkant afgeschuind
    wx = PCB_X + ACTIVE_X0 - WINDOW_MARGIN
    wy = PCB_Y + ACTIVE_Y0 - WINDOW_MARGIN
    ww, wh = ACTIVE_W + 2 * WINDOW_MARGIN, ACTIVE_H + 2 * WINDOW_MARGIN
    shell = shell.cut(box(wx, wy, -1, ww, wh, FRONT_T + 2))
    c = CHAMFER
    bot = Part.makePolygon([V(wx - c, wy - c, 0), V(wx + ww + c, wy - c, 0),
                            V(wx + ww + c, wy + wh + c, 0), V(wx - c, wy + wh + c, 0),
                            V(wx - c, wy - c, 0)])
    top = Part.makePolygon([V(wx, wy, c), V(wx + ww, wy, c), V(wx + ww, wy + wh, c),
                            V(wx, wy + wh, c), V(wx, wy, c)])
    shell = shell.cut(Part.makeLoft([bot, top], True))

    # voorgeboorde gaten voor de deksel-schroeven
    for x, y in CORNERS:
        shell = shell.cut(cyl(x, y, LID_IN - 10, PILOT_25 / 2, 10.1))

    # USB-opening in de onderwand, ter hoogte van de Pico-stekker
    usb_z = LID_IN - PICO_POST_H - PICO_T - 1.3
    shell = shell.cut(box(CX - USB_W / 2, -1, usb_z - USB_H / 2, USB_W, WALL + 2, USB_H))

    # schuin voetje onder de voorkant, zodat de voorrand ook op tafel rust
    # (tafelvlak: y = (z - DEPTH) * tan(TILT))
    foot = yz_prism([(0, 0), (0, LID_IN), ((LID_IN - DEPTH) * TAN, LID_IN), (-DEPTH * TAN, 0)],
                    0, OUT_W)
    shell = shell.fuse(foot)
    # USB-opening ook door het voetje heen
    shell = shell.cut(box(CX - USB_W / 2, -DEPTH * TAN - 1, usb_z - USB_H / 2,
                          USB_W, WALL + DEPTH * TAN + 2, USB_H))
    return shell.removeSplitter()


# ------------------------------------------------------------------ deksel
def make_back():
    lid = box(0, 0, LID_IN, OUT_W, OUT_H, LID_T)
    for x, y in CORNERS:
        lid = lid.cut(cyl(x, y, LID_IN - 1, CLEAR_25 / 2, LID_T + 2))

    # drukblokjes die het display tegen de voorkant houden (langs de lange zijden)
    pad = 5.0
    for px in (PCB_X + 1.0, PCB_X + PCB_W - 1.0 - pad):
        for fy in (0.3, 0.7):
            py = PCB_Y + PCB_H * fy - pad / 2
            lid = lid.fuse(box(px, py, MODULE_BACK, pad, pad, LID_IN - MODULE_BACK))

    # paaltjes voor de Pico W, USB-kant naar beneden tegen de onderwand
    px0, py0 = CX - PICO_W / 2, WALL + 1.0
    for hx, hy in PICO_HOLES:
        x, y = px0 + hx, py0 + hy
        lid = lid.fuse(cyl(x, y, LID_IN - PICO_POST_H, 2.5, PICO_POST_H))
        lid = lid.cut(cyl(x, y, LID_IN - PICO_POST_H - 1, PILOT_2 / 2, PICO_POST_H + 1))

    # doorgaande gaten voor de standaard
    for x, y in stand_screws():
        lid = lid.cut(cyl(x, y, LID_IN - 1, CLEAR_25 / 2, LID_T + 2))
    return lid.removeSplitter()


# ------------------------------------------------------------------ standaard
def stand_screws():
    return [(CX, 30.0), (CX, STAND_TOP - 15.0)]


def make_stand():
    z0 = DEPTH
    plate = box(CX - FIN_DX - FIN_T / 2, 0, z0, 2 * FIN_DX + FIN_T, STAND_TOP, STAND_PLATE_T)
    # vin: onderkant ligt op het tafelvlak y = (z - DEPTH) * tan(TILT)
    zf = DEPTH + STAND_LEN
    pts = [(0, z0), ((zf - DEPTH) * TAN, zf), (STAND_TOP, z0)]
    for dx in (-FIN_DX, FIN_DX):
        plate = plate.fuse(yz_prism(pts, CX + dx - FIN_T / 2, FIN_T))
    for x, y in stand_screws():
        plate = plate.cut(cyl(x, y, z0 + 0.8, PILOT_25 / 2, STAND_PLATE_T))
    return plate.removeSplitter()


def export(shape, name):
    os.makedirs(OUT, exist_ok=True)
    mesh = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.05, AngularDeflection=0.2)
    path = os.path.join(OUT, name)
    mesh.write(path)
    bb = shape.BoundBox
    print(f"{name}: {bb.XLength:.1f} x {bb.YLength:.1f} x {bb.ZLength:.1f} mm, "
          f"geldig={shape.isValid()}, volume={shape.Volume / 1000:.1f} cm3")


front, back, stand = make_front(), make_back(), make_stand()
export(front, "front.stl")
export(back, "back.stl")
export(stand, "stand.stl")
export(front.fuse(back).fuse(stand), "assembly.stl")

# controle: overlappen onderdelen?
for a, b, n in ((front, back, "front/back"), (back, stand, "back/stand"), (front, stand, "front/stand")):
    v = a.common(b).Volume
    print(f"overlap {n}: {v:.2f} mm3")
