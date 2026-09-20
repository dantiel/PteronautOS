# PteronautOS Cloud Build & Flasher — browser client (CoffeeScript).
# Drives the Cloudflare Worker /api/* endpoints and flashes over Web Serial
# with esptool-js. API_BASE defaults to same-origin (the worker serves this
# page); override cross-origin with ?api=<worker-url>.

import { ESPLoader, Transport } from "./vendor/esptool-js.js"

params = new URLSearchParams location.search
API_BASE = params.get("api") or document.body.dataset.apiBase or ""

$ = (sel) -> document.querySelector sel

els =
  buildBtn: $("#build-btn")
  flashBtn: $("#flash-btn")
  statusCard: $("#status-card")
  statusDot: $("#status-dot")
  statusText: $("#status-text")
  runLink: $("#run-link")
  progressWrap: $("#progress-wrap")
  progressBar: $("#progress-bar")
  log: $("#log")

currentRunId = null
firmwareBytes = null

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
  "#i18n_locales"
]

saveConfig = ->
  try
    cfg = {}
    cfg[id] = $(id).value for id in FIELD_IDS
    localStorage.setItem CONFIG_KEY, JSON.stringify cfg
  catch

restoreConfig = ->
  try
    cfg = JSON.parse localStorage.getItem CONFIG_KEY
    return unless cfg
    for id in FIELD_IDS
      $(id).value = cfg[id] if cfg[id]?
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

num = (id) -> parseInt $(id).value, 10

collectParams = ->
  mixer_profile: $("#mixer_profile").value
  regulatory_domain: $("#regulatory_domain").value
  binding_phrase: $("#binding_phrase").value.trim()
  auto_wifi_on_interval: num "#auto_wifi_on_interval"
  zephyrus_i2c_sda: num "#zephyrus_i2c_sda"
  zephyrus_i2c_scl: num "#zephyrus_i2c_scl"
  zephyrus_board_rotation: $("#zephyrus_board_rotation").value
  mushin_rx_pin: num "#mushin_rx_pin"
  mushin_tx_pin: num "#mushin_tx_pin"
  mushin_baud: num "#mushin_baud"
  rcvr_uart_baud: num "#rcvr_uart_baud"
  device_name: $("#device_name").value.trim()
  home_wifi_ssid: $("#home_wifi_ssid").value.trim()
  home_wifi_password: $("#home_wifi_password").value
  i18n_locales: $("#i18n_locales").value.trim()

startBuild = ->
  els.buildBtn.disabled = true
  els.flashBtn.disabled = true
  firmwareBytes = null
  currentRunId = null
  els.log.textContent = ""
  setStatus "Dispatching build…", "busy"
  els.runLink.classList.add "hidden"
  els.progressWrap.classList.add "hidden"

  try
    resp = await fetch API_BASE + "/api/build",
      method: "POST"
      headers:
        "Content-Type": "application/json"
      body: JSON.stringify collectParams()
    data = await resp.json()
    unless resp.ok
      throw new Error data.error or "HTTP " + resp.status

    currentRunId = data.run_id
    if data.html_url
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
    resp = await fetch API_BASE + "/api/build/" + currentRunId
    data = await resp.json()
    unless resp.ok
      throw new Error data.error or "HTTP " + resp.status

    unless data.ready
      setStatus "Building… (" + (data.status or "queued") + ")", "busy"
      setTimeout poll, 5000
      return

    unless data.conclusion is "success"
      setStatus "Build " + data.conclusion + " — check the GitHub run.", "fail"
      els.buildBtn.disabled = false
      return

    setStatus "Build complete — ready to flash.", "done"
    els.buildBtn.disabled = false
    els.flashBtn.disabled = false
  catch err
    setStatus "Polling error: " + err.message, "fail"
    els.buildBtn.disabled = false

downloadFirmware = ->
  setStatus "Downloading firmware…", "busy"
  resp = await fetch API_BASE + "/api/build/" + currentRunId + "/download"
  unless resp.ok
    err = await resp.json().catch ->
      error: "HTTP " + resp.status
    throw new Error err.error or "HTTP " + resp.status
  buf = await resp.arrayBuffer()
  firmwareBytes = new Uint8Array buf
  log "Firmware: " + firmwareBytes.length + " bytes (merged image @ 0x0000)"

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

els.buildBtn.addEventListener "click", startBuild
els.flashBtn.addEventListener "click", flash