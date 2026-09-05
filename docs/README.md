# PteronautOS GitHub Pages

Static site for the biomechanical pterosaur flight controller.

## Design

- **Fonts:** Turret Road for display text; the locally generated Turret Road
  Mono for code, diagrams, version strings, and technical data
- **Themes:** Dark default, light via `prefers-color-scheme`
- **Headline:** "PTERONAUTOS" with "PTERONAUT" white, "OS" in pale grey `#666`
- **Subhead:** "FLY NATURAL. CONTROL PRECISE." — 35% letter-spacing

## Development

Requires Ruby 3.0+, Bundler, and Node.js:

```bash
cd docs
bundle install
npm ci
```

Regenerating the local font additionally requires Python 3.9+:

```bash
python3 -m pip install -r requirements-fonts.txt
bundle exec rake fonts
```

## Building Locally

```bash
bundle exec haml render index.haml > index.html
bundle exec sass assets/css/style.sass assets/css/style.css
```

Or use Rake:

```bash
bundle exec rake build
```

Watch mode (requires separate terminals):

```bash
bundle exec haml --watch index.haml:index.html
bundle exec sass --watch assets/css/style.sass:assets/css/style.css
```

## Contributing

1. Edit `.haml` or `.sass` files
2. Test locally
3. Push — GitHub Actions compiles and deploys

## Stack

- Haml 6.x for markup
- Sass 3.x (indented syntax) for styles
- Haml and Sass-generated static pages
- CoffeeScript 2 source for interactive documentation, committed as compiled JavaScript
- FontTools-based, reproducible Turret Road Mono build with per-glyph optical overrides

## Turret Road Mono

The six self-hosted font weights in `assets/fonts/turret-road-mono/` are built
from the adjacent official OFL sources by
`scripts/monospace_turret_road.py`. The transformation keeps one 700-unit cell,
centres narrow outlines, fits only wide outlines, retains combining marks at
zero width, and removes kerning and default ligatures. Optical exceptions live
in `scripts/turret-road-mono-overrides.json`; its schema is documented beside
the generated fonts.

## Interactive documentation

The ferocity waveform explorer is authored in
`assets/js/ferocity-waveform.coffee`. It is compiled automatically by the Rake
build, or independently with:

```bash
npm run build:coffee
```

## License

Inherits PteronautOS / ExpressLRS lineage.
