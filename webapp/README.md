# PteronautOS Cloud Build & Flasher

A tiny web app that removes the console from the PteronautOS build/flash loop:

1. **Pick params** (mixer profile, regulatory domain, pins, binding phrase).
2. **Cloud build** — a GitHub Actions workflow compiles a single merged `.bin`.
3. **Web Serial flash** — `esptool-js` writes it straight to the ESP8285 from the browser.

No PlatformIO, no Python, no native app.

## Architecture

```
webapp (static)  ──POST /api/build──▶  Cloudflare Worker  ──▶  GitHub API
      │                                       │                 │
      │◀── poll /api/build/:id ───────────────┤                 │
      │◀── GET /api/build/:id/download ───────┤ (unzips artifact)│
      └──▶ esptool-js writeFlash ◀── raw .bin bytes
```

- **`.github/workflows/cloud-build.yml`** — `workflow_dispatch` build. Writes the
  chosen params into `src/user_defines.txt` (the ELRS-native flag mechanism),
  builds `PteronautOS_ESP8285_2400_RX`, and uploads `pteronautos.bin`.
- **`worker/`** — Cloudflare Worker. Holds the GitHub token server-side (never in
  the browser), triggers builds, polls status, extracts the `.bin` from the
  GitHub artifact zip (`fflate`), and serves the static webapp.
- **`webapp/`** — the frontend (vanilla JS + `esptool-js`).

The `.bin` is a **single merged image** (bootloader + app + FS), flashed at
`0x0000` — matching the ELRS `write_flash 0x0000 firmware.bin` model.

## Setup

### 1. GitHub token

Create a token with Actions access on the repo:

- **Classic PAT** → `repo` scope (simplest), or
- **Fine-grained PAT** → repository `dantiel/PteronautOS`, permission
  *Actions → Read and write*.

### 2. Deploy the worker

```bash
cd worker
npm install
npx wrangler login
npx wrangler secret put GITHUB_TOKEN   # paste the token
# optional: edit GITHUB_REPO / GITHUB_REF in wrangler.toml
npx wrangler deploy
```

The worker serves the webapp at the deployed `*.workers.dev` URL (or your custom
domain). That's the whole app — one deploy.

### 3. Local dev

```bash
cd worker
npm install
npx wrangler dev
# opens http://localhost:8787
```

## Params

| Param | Default | Notes |
|---|---|---|
| `mixer_profile` | `1` | `0–7`, see `OrnithopterConfig.h` |
| `regulatory_domain` | `EU_CE_2400` | `EU_CE_2400` (LBT) / `ISM_2400` |
| `binding_phrase` | — | empty = traditional binding |
| `zephyrus_i2c_sda` / `scl` | `1` / `3` | MPU6050 pins |
| `zephyrus_board_rotation` | `0` | `0–6` mounting orientation |
| `mushin_rx_pin` / `tx_pin` | `9` / `10` | RP2040 bridge |
| `mushin_baud` | `57600` | |
| `auto_wifi_on_interval` | `30` | seconds, `-1` disables |

## Notes & limitations

- Web Serial requires **Chrome / Edge 89+**.
- Flash params are fixed to the image header values: `dout` / `1MB` / `80m`.
- The `ZEPHYRUS_ENABLED` / `MUSHIN_ENABLED` toggles are intentionally **not**
  exposed yet: disabling Zephyrus does not currently compile (an `anchorGain`
  member is guarded by `#ifdef ZEPHYRUS_ENABLED` but used unconditionally).
