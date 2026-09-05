# Turret Road Mono

Turret Road Mono is PteronautOS's fixed-pitch adaptation of Turret Road. It is
generated from the six static Turret Road fonts distributed by Google Fonts
under the SIL Open Font License 1.1.

The transformation uses a 700-unit cell on the original 1000-unit em:

- spacing glyphs all receive the same 700-unit advance;
- zero-width and combining glyphs remain zero-width;
- outlines are optically centred in the cell;
- wide outlines are compressed only enough to retain 40-unit side bearings;
- vertical coordinates and vertical font metrics remain unchanged;
- stale TrueType hinting, pair kerning, and default ligatures are removed;
- font metadata identifies the result as `Turret Road Mono` and fixed-pitch.

Generate and validate the family from `docs/`:

```bash
python3 -m pip install -r requirements-fonts.txt
python3 scripts/monospace_turret_road.py
python3 scripts/monospace_turret_road.py --check
```

## Optical corrections

`scripts/turret-road-mono-overrides.json` accepts selectors as a glyph name,
one literal Unicode character, or `U+XXXX`. Each value can contain:

- `fit`: allow automatic fitting to the cell (default `true`);
- `scale_x`: requested horizontal scale before fit limiting (default `1.0`);
- `shift_x`: optical horizontal offset in font units (default `0`);
- `side_bearing`: per-glyph fitting margin in font units (default `40`).

Changing the advance per glyph is intentionally unsupported: that would stop
the result from being monospaced.

The original copyright and license are included as `OFL.txt` and also remain
beside the source binaries in `turret-road-source/OFL.txt`. Generated binaries
remain under that license.
