# PteronautOS GitHub Pages

Static site for the biomechanical pterosaur flight controller.

## Design

- **Font:** Turret Road (Google Fonts) — technical, industrial weight
- **Themes:** Dark default, light via `prefers-color-scheme`
- **Headline:** "PTERONAUTOS" with "PTERONAUT" white, "OS" in pale grey `#666`
- **Subhead:** "FLY NATURAL. CONTROL PRECISE." — 35% letter-spacing

## Development

Requires Ruby 3.0+ and Bundler:

```bash
cd docs
bundle install
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
- Zero client-side build — static HTML/CSS

## License

Inherits PteronautOS / ExpressLRS lineage.