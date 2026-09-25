# PteronautOS Cloud Build & Flasher — browser client (CoffeeScript).
# Drives a small Cloudflare Worker (worker/) that holds the GitHub token, then
# flashes over Web Serial with esptool-js. No secrets live in the browser.

import { ESPLoader, Transport } from "./vendor/esptool-js.js"

$ = (sel) -> document.querySelector sel

els =
  buildBtn: $("#build-btn")
  flashBtn: $("#flash-btn")
  downloadBtn: $("#download-btn")
  loadBtn: $("#load-btn")
  disconnectBtn: $("#disconnect-btn")
  fileInput: $("#firmware-file")
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
busy = false
activePort = null
activeTransport = null
builtFingerprint = null
serialSupported = "serial" of navigator

window.addEventListener "beforeunload", (e) ->
  if busy
    e.preventDefault()
    e.returnValue = ""

# The worker is reached same-origin when this page is served by the worker, or
# via ?api=https://<worker> when hosted elsewhere (e.g. GitHub Pages).
# DEFAULT_API_BASE is baked in at deploy time so the GitHub Pages copy "just
# works" for every visitor — nobody ever sees or types the worker URL. Leave ""
# and the page falls back to same-origin (worker-hosted) or ?api= override.
DEFAULT_API_BASE = "https://build.pteronautos.workers.dev"

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
  "#mushin_enabled"
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
  els.statusDot.className = "flasher-dot"
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
  mushin_enabled: $("#mushin_enabled").value
  rcvr_uart_baud: $("#rcvr_uart_baud").value
  device_name: $("#device_name").value.trim()
  home_wifi_ssid: $("#home_wifi_ssid").value.trim()
  home_wifi_password: $("#home_wifi_password").value
  i18n_locales: checked.join(",")

enableFirmware = ->
  els.downloadBtn.disabled = false
  els.flashBtn.disabled = false if serialSupported

# Stable fingerprint of the current build config — the IndexedDB cache key.
hashStr = (s) ->
  h = 0x811c9dc5
  for i in [0...s.length]
    h ^= s.charCodeAt i
    h = Math.imul(h, 0x01000193) >>> 0
  h.toString 16

# Browser cache (IndexedDB): a cloud-built image survives reloads — no rebuild.
DB_NAME = "pteronautos-fossil-etcher"
DB_VERSION = 1
STORE = "builds"
dbPromise = null

openDb = ->
  return dbPromise if dbPromise
  dbPromise = new Promise (resolve, reject) ->
    req = indexedDB.open DB_NAME, DB_VERSION
    req.onupgradeneeded = ->
      db = req.result
      db.createObjectStore STORE unless db.objectStoreNames.contains STORE
    req.onsuccess = -> resolve req.result
    req.onerror = -> reject req.error
  dbPromise

idbPut = (key, value) ->
  db = await openDb()
  new Promise (resolve, reject) ->
    tx = db.transaction STORE, "readwrite"
    tx.objectStore(STORE).put value, key
    tx.oncomplete = -> resolve()
    tx.onerror = -> reject tx.error

idbGet = (key) ->
  db = await openDb()
  new Promise (resolve, reject) ->
    tx = db.transaction STORE, "readonly"
    req = tx.objectStore(STORE).get key
    req.onsuccess = -> resolve req.result
    req.onerror = -> reject req.error

firmwareKey = ->
  hashStr JSON.stringify collectParams()

cacheFirmware = ->
  return unless firmwareBytes?
  try
    record =
      name: "pteronautos-firmware.bin"
      size: firmwareBytes.length
      ts: Date.now()
      blob: firmwareBytes.slice().buffer
    await idbPut firmwareKey(), record
    log "Saved build in this browser (" + firmwareBytes.length + " bytes) — reload and it's reused."
  catch err
    log "Couldn't cache build: " + err.message

restoreCachedBuild = ->
  return unless "indexedDB" of window
  try
    record = await idbGet firmwareKey()
    if record?.blob?
      firmwareBytes = new Uint8Array record.blob
      enableFirmware()
      log "Reusing saved build (" + record.size + " bytes) — no rebuild needed."
      setStatus "Saved build loaded — ready to flash or download.", "done"
  catch
    # no cache or storage blocked — nothing to load

loadFromDisk = (file) ->
  buf = await file.arrayBuffer()
  firmwareBytes = new Uint8Array buf
  enableFirmware()
  log "Loaded " + file.name + " (" + firmwareBytes.length + " bytes) from disk."
  setStatus "Firmware image loaded — ready to flash or download.", "done"

disconnect = ->
  try
    await activeTransport?.disconnect()
  catch
  try
    activePort?.close()
  catch
  activeTransport = null
  activePort = null
  els.disconnectBtn.classList.add "hidden"
  log "Disconnected."

startBuild = ->
  els.buildBtn.disabled = true
  els.flashBtn.disabled = true
  els.downloadBtn.disabled = true
  firmwareBytes = null
  currentRunId = null
  builtFingerprint = null
  els.log.textContent = ""
  setStatus "Dispatching build…", "busy"
  els.runLink.classList.add "hidden"
  els.progressWrap.classList.add "hidden"
  busy = true

  try
    params = collectParams()
    builtFingerprint = hashStr JSON.stringify params
    delay = Number params.auto_wifi_on_interval
    unless Number.isInteger(delay) and delay >= -1 and delay <= 2147483
      throw new Error "Wi-Fi auto-on interval must be -1 (disabled) or a nonnegative number of seconds."
    log "Wi-Fi fallback: " + (if delay is -1 then "disabled" else "#{delay} seconds (0 uses the firmware's 30-second default)")
    data = await apiFetch "/api/build",
      method: "POST"
      headers: {"Content-Type": "application/json"}
      body: JSON.stringify params
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
    busy = false

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
      busy = false
      return
    setStatus "Build complete — downloading image…", "busy"
    await downloadFirmware()
    await cacheFirmware()
    enableFirmware()
    setStatus "Build complete — cached in this browser. Ready to flash or download.", "done"
    els.buildBtn.disabled = false
    busy = false
  catch err
    setStatus "Polling error: " + err.message, "fail"
    els.buildBtn.disabled = false
    busy = false

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
  busy = true
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
    busy = false

terminal = ->
  clean: ->
    els.log.textContent = ""
  writeLine: (d) ->
    log d
  write: (d) ->
    els.log.textContent += d

flash = ->
  unless "serial" of navigator
    setStatus "Web Serial is not supported in this browser — use Chrome, Edge or Opera.", "fail"
    return

  els.flashBtn.disabled = true
  els.progressWrap.classList.remove "hidden"
  els.progressBar.style.width = "0%"
  busy = true

  try
    unless firmwareBytes?
      await downloadFirmware()

    setStatus "Connecting to device…", "busy"
    activePort = await navigator.serial.requestPort()

    activeTransport = new Transport activePort, true
    els.disconnectBtn.classList.remove "hidden"
    esploader = new ESPLoader
      transport: activeTransport
      baudrate: 115200
      terminal: terminal()
      debugLogging: false

    chip = await esploader.main("no_reset")
    log "Connected: " + chip

    setStatus "Flashing…", "busy"
    await esploader.writeFlash
      fileArray: [ { data: firmwareBytes, address: 0x0000 } ]
      flashMode: "dout"
      flashFreq: "40m"
      flashSize: "1MB"
      eraseAll: false
      compress: true
      reportProgress: (_fileIndex, written, total) ->
        pct = if total > 0 then Math.round(written / total * 100) else 0
        els.progressBar.style.width = pct + "%"
        els.statusText.textContent = "Flashing… " + pct + "%"

    log "Flashing complete. The receiver stays in the ROM bootloader — exit MEDITATION to boot the new firmware."
    await esploader.after "no_reset_stub"

    setStatus "Flashed successfully — exit MEDITATION (long-press BOOT / tap RESET) to boot.", "done"
    els.progressBar.style.width = "100%"
    await disconnect()
  catch err
    setStatus "Flash failed: " + err.message, "fail"
    log err.stack or err.message
  finally
    els.flashBtn.disabled = false
    busy = false

syncMushinFields = ->
  enabled = $("#mushin_enabled").value is "1"
  for el in document.querySelectorAll ".flasher-conditional"
    el.classList.toggle "hidden", not enabled

restoreConfig()
syncMushinFields()
restoreCachedBuild()

for id in FIELD_IDS
  $(id).addEventListener "input", saveConfig
  $(id).addEventListener "change", saveConfig

$("#mushin_enabled").addEventListener "change", syncMushinFields

for l in LOCALES
  $("#locale-#{l}").addEventListener "change", saveConfig

unless serialSupported
  els.serialWarning.classList.remove "hidden"
  els.flashBtn.classList.add "hidden"

els.buildBtn.addEventListener "click", startBuild
els.loadBtn.addEventListener "click", -> els.fileInput.click()
els.disconnectBtn.addEventListener "click", disconnect
els.fileInput.addEventListener "change", ->
  file = els.fileInput.files?[0]
  if file
    try
      await loadFromDisk file
    catch err
      setStatus "Load failed: " + err.message, "fail"
  els.fileInput.value = ""
els.flashBtn.addEventListener "click", flash
els.downloadBtn.addEventListener "click", saveFirmware