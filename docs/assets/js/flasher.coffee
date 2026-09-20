# PteronautOS Cloud Build & Flasher — browser client (CoffeeScript).
# Drives a small Cloudflare Worker (worker/) that holds the GitHub token, then
# flashes over Web Serial with esptool-js. No secrets live in the browser.

import { ESPLoader, Transport } from "./vendor/esptool-js.js"

$ = (sel) -> document.querySelector sel

els =
  buildBtn: $("#build-btn")
  flashBtn: $("#flash-btn")
  downloadBtn: $("#download-btn")
  serialWarning: $("#serial-warning")
  statusCard: $("#status-card")
  statusDot: $("#status-dot")
  statusText: $("#status-text")
  runLink: $("#run-link")
  progressWrap: $("#progress-wrap")
  progressBar: $("#progress-bar")
  log: $("#log")

currentRunId = null
firmwareBytes = null
serialSupported = "serial" of navigator

# The worker is reached same-origin when this page is served by the worker, or
# via ?api=https://<worker> when hosted elsewhere (e.g. GitHub Pages).
# DEFAULT_API_BASE is baked in at deploy time so the GitHub Pages copy "just
# works" for every visitor — nobody ever sees or types the worker URL. Leave ""
# and the page falls back to same-origin (worker-hosted) or ?api= override.
DEFAULT_API_BASE = "https://pteronautos-build.pteronautos.workers.dev"

API_BASE = (new URLSearchParams(location.search).get("api") or DEFAULT_API_BASE or "").replace /\/$/, ""

# Persist config on this device (localStorage) so values survive page reloads.
CONFIG_KEY = "pteronautos-flasher-config"
FIELD_IDS = [
  "#mixer_profile"
  "#regulatory_domain"
  "#binding_phrase"
  "#auto_wifi_on_interval"
  "#zephyrus_i2c_sda"
  "#zephyrus_i2c_scl"
  "#zephyrus_board_rotation"
  "#mushin_rx_pin"
  "#mushin_tx_pin"
  "#mushin_baud"
  "#rcvr_uart_baud"
  "#device_name"
  "#home_wifi_ssid"
  "#home_wifi_password"
]
LOCALES = ["pt", "en", "ru", "es", "de", "ko", "ja", "zh", "ar", "hi", "fr"]

sleep = (ms) -> new Promise (r) -> setTimeout r, ms

apiFetch = (path, init = {}) ->
  resp = await fetch API_BASE + path, init
  text = await resp.text()
  data = null
  try
    data = JSON.parse text
  catch
    data = null
  unless resp.ok
    throw new Error (data?.error or data?.message or text or "HTTP " + resp.status)
  data

saveConfig = ->
  try
    cfg = {}
    cfg[id] = $(id).value for id in FIELD_IDS
    cfg.locales = LOCALES.filter (l) -> $("#locale-#{l}").checked
    localStorage.setItem CONFIG_KEY, JSON.stringify cfg
  catch

restoreConfig = ->
  try
    cfg = JSON.parse localStorage.getItem CONFIG_KEY
    return unless cfg
    for id in FIELD_IDS
      $(id).value = cfg[id] if cfg[id]?
    if cfg.locales?
      for l in LOCALES
        $("#locale-#{l}").checked = l in cfg.locales
    else
      for l in LOCALES
        $("#locale-#{l}").checked = false
  catch

log = (line) ->
  els.log.textContent += line + "\n"
  els.log.scrollTop = els.log.scrollHeight

setStatus = (text, state) ->
  els.statusCard.classList.remove "hidden"
  els.statusText.textContent = text
  els.statusDot.className = "dot"
  els.statusDot.classList.add "done" if state is "done"
  els.statusDot.classList.add "fail" if state is "fail"

collectParams = ->
  checked = LOCALES.filter (l) -> $("#locale-#{l}").checked
  mixer_profile: $("#mixer_profile").value
  regulatory_domain: $("#regulatory_domain").value
  binding_phrase: $("#binding_phrase").value.trim()
  auto_wifi_on_interval: $("#auto_wifi_on_interval").value
  zephyrus_i2c_sda: $("#zephyrus_i2c_sda").value
  zephyrus_i2c_scl: $("#zephyrus_i2c_scl").value
  zephyrus_board_rotation: $("#zephyrus_board_rotation").value
  mushin_rx_pin: $("#mushin_rx_pin").value
  mushin_tx_pin: $("#mushin_tx_pin").value
  mushin_baud: $("#mushin_baud").value
  rcvr_uart_baud: $("#rcvr_uart_baud").value
  device_name: $("#device_name").value.trim()
  home_wifi_ssid: $("#home_wifi_ssid").value.trim()
  home_wifi_password: $("#home_wifi_password").value
  i18n_locales: checked.join(",")

startBuild = ->
  els.buildBtn.disabled = true
  els.flashBtn.disabled = true
  els.downloadBtn.disabled = true
  firmwareBytes = null
  currentRunId = null
  els.log.textContent = ""
  setStatus "Dispatching build…", "busy"
  els.runLink.classList.add "hidden"
  els.progressWrap.classList.add "hidden"

  try
    data = await apiFetch "/api/build",
      method: "POST"
      headers: {"Content-Type": "application/json"}
      body: JSON.stringify collectParams()
    unless data?.run_id
      throw new Error "Build server returned no run id — is the worker deployed?"
    currentRunId = data.run_id
    els.runLink.href = data.html_url
    els.runLink.classList.remove "hidden"
    setStatus "Build queued — waiting for GitHub Actions…", "busy"
    poll()
  catch err
    setStatus "Build failed: " + err.message, "fail"
    els.buildBtn.disabled = false

poll = ->
  return unless currentRunId
  try
    data = await apiFetch "/api/build/" + currentRunId
    unless data.ready
      setStatus "Building… (" + (data.status or "queued") + ")", "busy"
      setTimeout poll, 5000
      return
    unless data.conclusion is "success"
      setStatus "Build " + data.conclusion + " — check the GitHub run.", "fail"
      els.buildBtn.disabled = false
      return
    if serialSupported
      setStatus "Build complete — ready to flash.", "done"
      els.flashBtn.disabled = false
    else
      setStatus "Build complete — ready to download.", "done"
      els.downloadBtn.disabled = false
    els.buildBtn.disabled = false
  catch err
    setStatus "Polling error: " + err.message, "fail"
    els.buildBtn.disabled = false

downloadFirmware = ->
  setStatus "Downloading firmware…", "busy"
  resp = await fetch API_BASE + "/api/build/" + currentRunId + "/download"
  unless resp.ok
    text = await resp.text()
    throw new Error "Download failed (HTTP " + resp.status + "): " + text
  firmwareBytes = new Uint8Array await resp.arrayBuffer()
  log "Firmware: " + firmwareBytes.length + " bytes (merged image @ 0x0000)"

saveFirmware = ->
  els.downloadBtn.disabled = true
  try
    unless firmwareBytes?
      await downloadFirmware()
    blob = new Blob [firmwareBytes], type: "application/octet-stream"
    url = URL.createObjectURL blob
    a = document.createElement "a"
    a.href = url
    a.download = "pteronautos-firmware.bin"
    document.body.appendChild a
    a.click()
    a.remove()
    URL.revokeObjectURL url
    log "Saved pteronautos-firmware.bin (" + firmwareBytes.length + " bytes)"
  catch err
    setStatus "Download failed: " + err.message, "fail"
  finally
    els.downloadBtn.disabled = false

terminal = ->
  clean: ->
    els.log.textContent = ""
  writeLine: (d) ->
    log d
  write: (d) ->
    els.log.textContent += d

flash = ->
  unless "serial" of navigator
    setStatus "Web Serial is not supported in this browser — use Chrome or Edge.", "fail"
    return

  els.flashBtn.disabled = true
  els.progressWrap.classList.remove "hidden"
  els.progressBar.style.width = "0%"

  try
    unless firmwareBytes?
      await downloadFirmware()

    setStatus "Connecting to device…", "busy"
    port = await navigator.serial.requestPort()
    await port.open baudRate: 115200

    transport = new Transport port, true
    esploader = new ESPLoader
      transport: transport
      baudrate: 115200
      terminal: terminal()
      debugLogging: false

    chip = await esploader.main()
    log "Connected: " + chip

    setStatus "Flashing…", "busy"
    await esploader.writeFlash
      fileArray: [ { data: firmwareBytes, address: 0x0000 } ]
      flashMode: "dout"
      flashFreq: "80m"
      flashSize: "1MB"
      eraseAll: false
      compress: true
      reportProgress: (_fileIndex, written, total) ->
        pct = if total > 0 then Math.round(written / total * 100) else 0
        els.progressBar.style.width = pct + "%"
        els.statusText.textContent = "Flashing… " + pct + "%"

    log "Flashing complete. Resetting…"
    await esploader.after "hard_reset"

    setStatus "Flashed successfully.", "done"
    els.progressBar.style.width = "100%"
    await port.close()
  catch err
    setStatus "Flash failed: " + err.message, "fail"
    log err.stack or err.message
  finally
    els.flashBtn.disabled = false

restoreConfig()

for id in FIELD_IDS
  $(id).addEventListener "input", saveConfig
  $(id).addEventListener "change", saveConfig

for l in LOCALES
  $("#locale-#{l}").addEventListener "change", saveConfig

unless serialSupported
  els.serialWarning.classList.remove "hidden"
  els.flashBtn.classList.add "hidden"
  els.downloadBtn.classList.remove "hidden"

els.buildBtn.addEventListener "click", startBuild
els.flashBtn.addEventListener "click", flash
els.downloadBtn.addEventListener "click", saveFirmware