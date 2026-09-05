#!/usr/bin/env python3
"""Build the fixed-pitch Turret Road Mono family from the OFL sources.

The transformation is deliberately conservative: every spacing glyph receives
one common advance, narrow outlines keep their proportions and are centred, and
only outlines that do not fit the cell are compressed horizontally. Vertical
coordinates and font-wide vertical metrics are never changed.
"""

from __future__ import annotations

import argparse
import json
import shutil
import unicodedata
from pathlib import Path
from typing import Any

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

# Importing subset installs the OpenType layout pruning helpers on TTFont tables.
import fontTools.subset  # noqa: F401


DEFAULT_FAMILY = "Turret Road Mono"
DEFAULT_CELL_WIDTH = 700
DEFAULT_SIDE_BEARING = 40
WEIGHT_ORDER = {
    "ExtraLight": 200,
    "Light": 300,
    "Regular": 400,
    "Medium": 500,
    "Bold": 700,
    "ExtraBold": 800,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Turn the Turret Road OFL binaries into Turret Road Mono."
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=Path(__file__).parents[1] / "assets/fonts/turret-road-source",
        help="Directory containing the original TurretRoad-*.ttf files.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(__file__).parents[1] / "assets/fonts/turret-road-mono",
        help="Directory for the generated TTF and WOFF2 files.",
    )
    parser.add_argument(
        "--overrides",
        type=Path,
        default=Path(__file__).with_name("turret-road-mono-overrides.json"),
        help="JSON file containing defaults and optional per-glyph corrections.",
    )
    parser.add_argument("--family", default=DEFAULT_FAMILY)
    parser.add_argument("--cell-width", type=int)
    parser.add_argument("--side-bearing", type=int)
    parser.add_argument(
        "--ttf-only",
        action="store_true",
        help="Skip WOFF2 output (useful when the optional Brotli module is absent).",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate existing output files instead of rebuilding them.",
    )
    return parser.parse_args()


def load_config(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"defaults": {}, "glyphs": {}}
    with path.open(encoding="utf-8") as handle:
        config = json.load(handle)
    if not isinstance(config.get("defaults", {}), dict):
        raise ValueError("overrides.defaults must be an object")
    if not isinstance(config.get("glyphs", {}), dict):
        raise ValueError("overrides.glyphs must be an object")
    return config


def resolve_overrides(font: TTFont, raw: dict[str, Any]) -> dict[str, dict[str, Any]]:
    """Resolve glyph names, U+XXXX selectors, and one-character selectors."""
    glyph_order = set(font.getGlyphOrder())
    cmap = font.getBestCmap() or {}
    resolved: dict[str, dict[str, Any]] = {}
    for selector, values in raw.items():
        if not isinstance(values, dict):
            raise ValueError(f"override {selector!r} must be an object")
        if selector in glyph_order:
            name = selector
        elif selector.upper().startswith("U+"):
            name = cmap.get(int(selector[2:], 16))
        elif len(selector) == 1:
            name = cmap.get(ord(selector))
        else:
            name = None
        if not name:
            raise ValueError(f"override selector {selector!r} does not resolve to a glyph")
        unknown = set(values) - {"fit", "scale_x", "shift_x", "side_bearing"}
        if unknown:
            raise ValueError(f"unknown keys for {selector!r}: {', '.join(sorted(unknown))}")
        resolved[name] = values
    return resolved


def glyph_bounds(glyph_set: Any, name: str) -> tuple[float, float, float, float] | None:
    pen = BoundsPen(glyph_set)
    glyph_set[name].draw(pen)
    return pen.bounds


def is_combining_glyph(font: TTFont, name: str) -> bool:
    cmap = font.getBestCmap() or {}
    codepoints = [codepoint for codepoint, glyph_name in cmap.items() if glyph_name == name]
    return bool(codepoints) and all(unicodedata.combining(chr(cp)) for cp in codepoints)


def remove_layout_feature(font: TTFont, table_tag: str, feature_tag: str) -> None:
    if table_tag not in font:
        return
    table = font[table_tag]
    feature_list = getattr(table.table, "FeatureList", None)
    if not feature_list:
        return
    keep = {
        record.FeatureTag
        for record in feature_list.FeatureRecord
        if record.FeatureTag != feature_tag
    }
    table.subset_feature_tags(keep)
    table.prune_lookups()


def set_name(font: TTFont, name_id: int, value: str) -> None:
    names = font["name"]
    names.removeNames(nameID=name_id)
    names.setName(value, name_id, 3, 1, 0x0409)
    names.setName(value, name_id, 1, 0, 0)


def rename_font(font: TTFont, family: str, style: str) -> None:
    postscript_family = "".join(ch for ch in family if ch.isalnum())
    postscript_style = "".join(ch for ch in style if ch.isalnum())
    full_name = f"{family} {style}"
    set_name(font, 1, family)
    set_name(font, 2, style)
    set_name(font, 3, f"1.000;PTERONAUTOS;{postscript_family}-{postscript_style}")
    set_name(font, 4, full_name)
    set_name(font, 6, f"{postscript_family}-{postscript_style}")
    set_name(font, 16, family)
    set_name(font, 17, style)


def monospacify(
    source_path: Path,
    output_path: Path,
    family: str,
    style: str,
    cell_width: int,
    side_bearing: int,
    raw_overrides: dict[str, Any],
) -> dict[str, int]:
    source_font = TTFont(source_path, recalcBBoxes=True, recalcTimestamp=False)
    font = TTFont(source_path, recalcBBoxes=True, recalcTimestamp=False)
    source_glyphs = source_font.getGlyphSet()
    overrides = resolve_overrides(source_font, raw_overrides)
    metrics = font["hmtx"].metrics
    fitted = 0
    spacing = 0
    zero_width = 0

    for name in font.getGlyphOrder():
        original_advance, _ = source_font["hmtx"].metrics[name]
        bounds = glyph_bounds(source_glyphs, name)

        # True combining marks and source zero-width glyphs must stay zero-width.
        if original_advance == 0 or is_combining_glyph(source_font, name):
            metrics[name] = (0, metrics[name][1])
            zero_width += 1
            continue

        spacing += 1
        if bounds is None:
            metrics[name] = (cell_width, 0)
            continue

        x_min, _, x_max, _ = bounds
        ink_width = x_max - x_min
        correction = overrides.get(name, {})
        glyph_bearing = float(correction.get("side_bearing", side_bearing))
        fit = bool(correction.get("fit", True))
        requested_scale = float(correction.get("scale_x", 1.0))
        shift_x = float(correction.get("shift_x", 0.0))
        if requested_scale <= 0:
            raise ValueError(f"scale_x for {name!r} must be greater than zero")
        if glyph_bearing < 0 or glyph_bearing * 2 >= cell_width:
            raise ValueError(f"side_bearing for {name!r} does not fit the cell")

        scale_x = requested_scale
        available_ink = cell_width - 2 * glyph_bearing
        if fit and ink_width > 0:
            scale_x = min(scale_x, available_ink / ink_width)
        if scale_x < 0.999999:
            fitted += 1

        transformed_width = ink_width * scale_x
        offset_x = (cell_width - transformed_width) / 2 - x_min * scale_x + shift_x

        # Decompose components before transformation so component glyphs do not
        # receive the horizontal transform twice when they are rebuilt later.
        recording = DecomposingRecordingPen(source_glyphs)
        source_glyphs[name].draw(recording)
        destination = TTGlyphPen(None)
        recording.replay(TransformPen(destination, (scale_x, 0, 0, 1, offset_x, 0)))
        font["glyf"][name] = destination.glyph()
        metrics[name] = (cell_width, round((cell_width - transformed_width) / 2 + shift_x))

    # Hinting no longer matches transformed outlines. Browser rasterizers handle
    # this unhinted webfont consistently, so remove stale programs explicitly.
    for tag in ("fpgm", "prep", "cvt "):
        if tag in font:
            del font[tag]
    if hasattr(font["maxp"], "maxSizeOfInstructions"):
        font["maxp"].maxSizeOfInstructions = 0

    remove_layout_feature(font, "GPOS", "kern")
    remove_layout_feature(font, "GSUB", "liga")
    rename_font(font, family, style)

    font["post"].isFixedPitch = 1
    font["OS/2"].xAvgCharWidth = cell_width
    font["OS/2"].panose.bProportion = 9
    font["hhea"].advanceWidthMax = cell_width

    output_path.parent.mkdir(parents=True, exist_ok=True)
    font.save(output_path, reorderTables=True)
    source_font.close()
    font.close()
    return {"spacing": spacing, "zero_width": zero_width, "fitted": fitted}


def name_value(font: TTFont, name_id: int) -> str:
    record = font["name"].getName(name_id, 3, 1, 0x0409)
    return record.toUnicode() if record else ""


def feature_tags(font: TTFont, table_tag: str) -> set[str]:
    if table_tag not in font or not font[table_tag].table.FeatureList:
        return set()
    return {
        record.FeatureTag
        for record in font[table_tag].table.FeatureList.FeatureRecord
    }


def validate_font(path: Path, family: str, cell_width: int) -> None:
    font = TTFont(path)
    advances = {advance for advance, _ in font["hmtx"].metrics.values() if advance != 0}
    failures: list[str] = []
    if advances != {cell_width}:
        failures.append(f"spacing advances are {sorted(advances)}, expected {cell_width}")
    if font["post"].isFixedPitch != 1:
        failures.append("post.isFixedPitch is not 1")
    if font["OS/2"].panose.bProportion != 9:
        failures.append("PANOSE proportion is not monospaced")
    if name_value(font, 1) != family:
        failures.append(f"family is {name_value(font, 1)!r}, expected {family!r}")
    if "kern" in feature_tags(font, "GPOS"):
        failures.append("GPOS still contains the kern feature")
    if "liga" in feature_tags(font, "GSUB"):
        failures.append("GSUB still contains the default liga feature")
    font.close()
    if failures:
        raise ValueError(f"{path}: " + "; ".join(failures))


def style_from_path(path: Path) -> str:
    style = path.stem.removeprefix("TurretRoad-")
    if style not in WEIGHT_ORDER:
        raise ValueError(f"unsupported Turret Road style in {path.name}")
    return style


def main() -> None:
    args = parse_args()
    config = load_config(args.overrides)
    defaults = config.get("defaults", {})
    cell_width = args.cell_width or int(defaults.get("cell_width", DEFAULT_CELL_WIDTH))
    side_bearing = args.side_bearing or int(
        defaults.get("side_bearing", DEFAULT_SIDE_BEARING)
    )
    raw_overrides = config.get("glyphs", {})
    args.output_dir.mkdir(parents=True, exist_ok=True)

    sources = sorted(
        args.source_dir.glob("TurretRoad-*.ttf"),
        key=lambda path: WEIGHT_ORDER.get(style_from_path(path), 999),
    )
    if not sources:
        raise FileNotFoundError(f"no TurretRoad-*.ttf files found in {args.source_dir}")

    source_license = args.source_dir / "OFL.txt"
    output_license = args.output_dir / "OFL.txt"
    if args.check:
        if not output_license.exists():
            raise FileNotFoundError(f"generated family is missing {output_license}")
    else:
        if not source_license.exists():
            raise FileNotFoundError(f"source family is missing {source_license}")
        shutil.copyfile(source_license, output_license)

    for source in sources:
        style = style_from_path(source)
        stem = f"TurretRoadMono-{style}"
        ttf_path = args.output_dir / f"{stem}.ttf"
        woff2_path = args.output_dir / f"{stem}.woff2"
        if args.check:
            validate_font(ttf_path, args.family, cell_width)
            if not args.ttf_only:
                validate_font(woff2_path, args.family, cell_width)
            print(f"checked {stem}")
            continue

        report = monospacify(
            source,
            ttf_path,
            args.family,
            style,
            cell_width,
            side_bearing,
            raw_overrides,
        )
        validate_font(ttf_path, args.family, cell_width)
        if not args.ttf_only:
            webfont = TTFont(ttf_path, recalcTimestamp=False)
            webfont.flavor = "woff2"
            webfont.save(woff2_path, reorderTables=True)
            webfont.close()
            validate_font(woff2_path, args.family, cell_width)
        print(
            f"built {stem}: {report['spacing']} spacing, "
            f"{report['zero_width']} zero-width, {report['fitted']} fitted"
        )


if __name__ == "__main__":
    main()
