#!/usr/bin/env python3
"""tools/sharp_sprites/pack.py -- bake ext.sharp_ui's magnified-sheet sprites into the exe (NOT RETAIL;
docs/extensions/handins/SHARP_SPRITES.md).

usage: python -I tools/sharp_sprites/pack.py [--scale N]
           rewrites src/ext/sharp_ui/sharp_sprites_data_<N>x.h from src/ext/sharp_ui/art/<N>x/ and sheet_map.json
       python -I tools/sharp_sprites/pack.py --map <ToolKit>/resource/Dialog/SpriteSheet.rsc
           rewrites sheet_map.json from the mod kit's sprite-sheet source (maintainer only)

The game draws its dialog sprites from ONE sheet, Dialog/SpriteSheet.rsc (about 550 sprites). A sprite there
is a NAME and a source picture, or a sub-rectangle of one (the nine pieces of a frame, the three of a scroll
bar). sheet_map.json is that table, cut down to the sprites we have art for: name, source, rectangle --
identifiers and coordinates, no pixels. This script cuts each sprite's piece out of our N x art (rectangle
times N) and writes the generated header the exe compiles in: one straight RGBA8 array per sheet sprite,
looked up by name at run time (src/ext/sharp_ui/sharp_sheet.h). A second table, kExtSprites, does the same
for our own sheet, src/ext/ui/Dialog/ExtensionsSprites.rsc: every art/<N>x/Extensions/<File>.png (drawn by
tools/ext_sprites.py) under the sprite name that sheet gives Sprite/Extensions/<File>.png. One header per
factor. The PNGs under art/ are the source of truth; regenerate after a bake and commit the header.

Decodes PNGs with Pillow.
"""
import argparse
import json
import pathlib
import re

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MAP = HERE / "sheet_map.json"
EXT_SHEET = ROOT / "src" / "ext" / "ui" / "Dialog" / "ExtensionsSprites.rsc"


def out_path(scale):
    return ROOT / "src" / "ext" / "sharp_ui" / ("sharp_sprites_data_%dx.h" % scale)


def write_map(rsc):
    """Every sheet entry whose source is a picture we have art for, with its sub-rectangle if it has one."""
    art = ROOT / "src" / "ext" / "sharp_ui" / "art" / "2x"
    text = pathlib.Path(rsc).read_text(encoding="latin-1")
    entries = re.findall(r'\{\s*String _name = "([^"]+)";\s*String _source = "([^"]+)";([^}]*)\}', text)
    out = []
    for name, source, rest in entries:
        rel = source.replace("\\", "/")
        assert rel.startswith("Sprite/"), rel
        rel = rel[len("Sprite/"):-len(".png")]
        if not (art / (rel + ".png")).exists():
            continue
        fields = dict(re.findall(r"int _(\w+) = (-?\d+)", rest))
        e = {"name": name, "art": rel}
        if fields:
            e["rect"] = [int(fields["x"]), int(fields["y"]), int(fields["width"]), int(fields["height"])]
        out.append(e)
    MAP.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8", newline="\n")
    print("pack: sheet_map.json, %d of %d sheet sprites have art" % (len(out), len(entries)))


def ext_entries(art):
    """Our own sheet's sprites that have art at this factor: art/<N>x/Extensions/<File>.png under the name
    ExtensionsSprites.rsc gives Sprite/Extensions/<File>.png."""
    text = EXT_SHEET.read_text(encoding="latin-1")
    names = {}
    for name, source in re.findall(r'String _name = "([^"]+)";\s*String _source = "([^"]+)";', text):
        names[source.replace("\\", "/")] = name
    out = []
    for png in sorted((art / "Extensions").glob("*.png")):
        source = "Sprite/Extensions/" + png.name
        assert source in names, "%s is not in %s" % (source, EXT_SHEET.name)
        out.append({"name": names[source], "art": "Extensions/" + png.stem})
    return out


def c_array(name, data):
    lines = []
    for i in range(0, len(data), 32):
        lines.append("    " + ",".join(str(b) for b in data[i:i + 32]) + ",")
    return "inline constexpr uint8_t %s[%d] = {\n%s\n};\n" % (name, len(data), "\n".join(lines))


def pack_table(art, scale, entries, prefix):
    """(arrays, table rows, bytes of pixels) of one table."""
    arrays = []
    table = []
    total = 0
    for i, e in enumerate(entries):
        img = Image.open(art / (e["art"] + ".png")).convert("RGBA")
        if "rect" in e:
            x, y, w, h = e["rect"]
            box = (x * scale, y * scale, (x + w) * scale, (y + h) * scale)
            assert box[2] <= img.size[0] and box[3] <= img.size[1], (e, img.size)
            img = img.crop(box)
        w, h = img.size
        assert w % scale == 0 and h % scale == 0, (e, img.size)
        data = img.tobytes()
        total += len(data)
        ident = "%s_%d_%s" % (prefix, i, re.sub(r"\W", "_", e["name"]))
        arrays.append("// %s: %s%s, %d x %d\n" % (
            e["name"], e["art"], " rect %s" % e["rect"] if "rect" in e else "", w, h) + c_array(ident, data))
        table.append('    {"%s", %d, %d, %s},' % (e["name"], w, h, ident))
    return arrays, table, total


def write_header(scale):
    art = ROOT / "src" / "ext" / "sharp_ui" / "art" / ("%dx" % scale)
    out = out_path(scale)
    arrays, table, total = pack_table(art, scale, json.loads(MAP.read_text(encoding="utf-8")), "kRgba")
    ext_arrays, ext_table, ext_total = pack_table(art, scale, ext_entries(art), "kExtRgba")
    head = (
        "// src/ext/sharp_ui/%s -- GENERATED by tools/sharp_sprites/pack.py; do not edit.\n"
        "// NOT RETAIL (docs/extensions/handins/SHARP_SPRITES.md): our art at %d x the sprites' size, straight\n"
        "// RGBA8 (row-major, top-down), one array per sprite NAME. kSprites: sprites of the game's\n"
        "// Dialog/SpriteSheet.rsc (third-party icons, tools/sharp_sprites/svg/LICENSES.md, and our own drawing,\n"
        "// tools/sharp_sprites/chrome.py). kExtSprites: sprites of our own Dialog/ExtensionsSprites.rsc\n"
        "// (tools/ext_sprites.py). The PNGs under src/ext/sharp_ui/art/ are the source.\n"
        "#pragma once\n\n#include <cstdint>\n\n#include \"ext/sharp_ui/sharp_sheet.h\"\n\n"
        "namespace Ext {\nnamespace SharpUI {\nnamespace SpritesData%dx {\n\n"
        "inline constexpr int kScale = %d;\n\n" % (out.name, scale, scale, scale))
    tail = ("inline constexpr ArtSprite kSprites[] = {\n%s\n};\ninline constexpr int kSpriteCount = %d;\n\n"
            "inline constexpr ArtSprite kExtSprites[] = {\n%s\n};\ninline constexpr int kExtSpriteCount = %d;\n\n"
            "}  // namespace SpritesData%dx\n}  // namespace SharpUI\n}  // namespace Ext\n"
            % ("\n".join(table), len(table), "\n".join(ext_table), len(ext_table), scale))
    out.write_text(head + "\n".join(arrays + ext_arrays) + "\n" + tail, encoding="utf-8", newline="\n")
    print("pack: %s, %d + %d sprites, %d KB of pixels" % (
        out.name, len(table), len(ext_table), (total + ext_total) // 1024))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scale", type=int, default=2, help="the art scale to pack (default 2)")
    ap.add_argument("--map", default=None, metavar="SPRITESHEET_RSC",
                    help="rewrite sheet_map.json from the kit's resource/Dialog/SpriteSheet.rsc")
    args = ap.parse_args()
    if args.map:
        write_map(args.map)
    write_header(args.scale)


if __name__ == "__main__":
    main()
