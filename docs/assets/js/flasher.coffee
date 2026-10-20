# PteronautOS Cloud Build & Flasher — browser client (CoffeeScript).
# Drives a small Cloudflare Worker (worker/) that holds the GitHub token, then
# flashes over Web Serial with esptool-js. No secrets live in the browser.

import { ESPLoader, Transport } from "./vendor/esptool-js.js"

$ = (sel) -> document.querySelector sel

els =
  buildBtn: $("#build-btn")
  flashBtn: $("#flash-btn")
  downloadBtn: $("#download-btn")
  downloadGzBtn: $("#download-gz-btn")
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
  specimensList: $("#specimens-list")
  clearSpecimensBtn: $("#clear-specimens-btn")

currentRunId = null
currentBuildInfo = null
firmwareBytes = null
busy = false
activePort = null
activeTransport = null
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
  "#hardware_target"
  "#zephyrus_enabled"
  "#zephyrus_mode"
  "#zephyrus_i2c_pre_detect"
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
    for id in FIELD_IDS
      el = $(id)
      cfg[id] = if el.type is "checkbox" then el.checked else el.value
    cfg.locales = LOCALES.filter (l) -> $("#locale-#{l}").checked
    localStorage.setItem CONFIG_KEY, JSON.stringify cfg
  catch

restoreConfig = ->
  try
    cfg = JSON.parse localStorage.getItem CONFIG_KEY
    return unless cfg
    for id in FIELD_IDS
      el = $(id)
      if el.type is "checkbox"
        el.checked = cfg[id] if cfg[id]?
      else
        el.value = cfg[id] if cfg[id]?
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
  zeph = if $("#zephyrus_enabled").checked then "1" else "0"
  # Gyro off ⇒ no reason to keep the WiFi fallback polling alive.
  wifi = if zeph is "0" then "-1" else $("#auto_wifi_on_interval").value
  mixer_profile: $("#mixer_profile").value
  regulatory_domain: $("#regulatory_domain").value
  binding_phrase: $("#binding_phrase").value.trim()
  auto_wifi_on_interval: wifi
  zephyrus_i2c_sda: $("#zephyrus_i2c_sda").value
  zephyrus_i2c_scl: $("#zephyrus_i2c_scl").value
  zephyrus_board_rotation: $("#zephyrus_board_rotation").value
  hardware_target: $("#hardware_target").value
  zephyrus_enabled: zeph
  zephyrus_mode: $("#zephyrus_mode").value
  zephyrus_i2c_pre_detect: (if $("#zephyrus_i2c_pre_detect").checked then "1" else "0")
  rcvr_uart_baud: $("#rcvr_uart_baud").value
  device_name: $("#device_name").value.trim()
  home_wifi_ssid: $("#home_wifi_ssid").value.trim()
  home_wifi_password: $("#home_wifi_password").value
  i18n_locales: checked.join(",")

enableFirmware = ->
  els.downloadBtn.disabled = false
  els.downloadGzBtn.disabled = false
  els.flashBtn.disabled = false if serialSupported

# Firmware fossil storage (IndexedDB): every cloud build is kept with its full
# config snapshot + timestamp, so it can be re-selected, flashed or dismissed.
DB_NAME = "pteronautos-fossil-etcher"
DB_VERSION = 2
BUILD_STORE = "builds"          # legacy cache store (kept for schema upgrades)
SPECIMEN_STORE = "specimens"    # user-facing firmware fossils (keyPath: "id")
dbPromise = null

openDb = ->
  return dbPromise if dbPromise
  dbPromise = new Promise (resolve, reject) ->
    req = indexedDB.open DB_NAME, DB_VERSION
    req.onupgradeneeded = ->
      db = req.result
      db.createObjectStore BUILD_STORE unless db.objectStoreNames.contains BUILD_STORE
      unless db.objectStoreNames.contains SPECIMEN_STORE
        db.createObjectStore SPECIMEN_STORE, keyPath: "id"
    req.onsuccess = -> resolve req.result
    req.onerror = -> reject req.error
  dbPromise

idbPutSpecimen = (rec) ->
  db = await openDb()
  new Promise (resolve, reject) ->
    tx = db.transaction SPECIMEN_STORE, "readwrite"
    tx.objectStore(SPECIMEN_STORE).put rec
    tx.oncomplete = -> resolve()
    tx.onerror = -> reject tx.error

idbAllSpecimens = ->
  db = await openDb()
  new Promise (resolve, reject) ->
    tx = db.transaction SPECIMEN_STORE, "readonly"
    req = tx.objectStore(SPECIMEN_STORE).getAll()
    req.onsuccess = -> resolve req.result or []
    req.onerror = -> reject req.error

idbDeleteSpecimen = (id) ->
  db = await openDb()
  new Promise (resolve, reject) ->
    tx = db.transaction SPECIMEN_STORE, "readwrite"
    tx.objectStore(SPECIMEN_STORE).delete id
    tx.oncomplete = -> resolve()
    tx.onerror = -> reject tx.error

idbClearSpecimens = ->
  db = await openDb()
  new Promise (resolve, reject) ->
    tx = db.transaction SPECIMEN_STORE, "readwrite"
    tx.objectStore(SPECIMEN_STORE).clear()
    tx.oncomplete = -> resolve()
    tx.onerror = -> reject tx.error

SPECIMEN_KEY = "pteronautos-selected-specimen"
selectedSpecimenId = null

MIXER_LABELS =
  "0": "2 wings"
  "1": "2 wings + rudder"
  "2": "4 wings"
  "3": "V-tail + rudder"
  "4": "motor + V-tail"
  "5": "motor + V-tail + rudder"
  "6": "elevator + rudder"
  "7": "motor + elevator + rudder"

HARDWARE_LABELS =
  "pwmp7": "PWMP7"
  "ep2pwm": "EP2 PWM"
  "mushin": "EP2 + RP2040"

fmtSize = (n) ->
  if n >= 1048576 then (n / 1048576).toFixed(1) + " MB" else Math.round(n / 1024) + " KB"

fmtWhen = (ts) ->
  new Date(ts).toLocaleString()

specimenMeta = (rec) ->
  p = rec.params or {}
  parts = [fmtWhen rec.ts]
  parts.push (MIXER_LABELS[p.mixer_profile] or "mixer " + p.mixer_profile)
  parts.push (if p.regulatory_domain is "EU_CE_2400" then "EU CE" else "ISM")
  parts.push (HARDWARE_LABELS[p.hardware_target] or p.hardware_target)
  parts.push (if p.zephyrus_enabled is "1" then "Zephyrus" else "no gyro")
  parts.push p.device_name if p.device_name
  parts.push rec.version if rec.version
  parts.push fmtSize rec.size
  parts.join " · "

saveSpecimen = ->
  return unless firmwareBytes?
  try
    rec =
      id: "s" + Date.now().toString(36) + "-" + Math.random().toString(36).slice(2, 7)
      ts: Date.now()
      name: "pteronautos-firmware.bin"
      size: firmwareBytes.length
      params: collectParams()
      version: currentBuildInfo?.version
      blob: firmwareBytes.slice().buffer
    await idbPutSpecimen rec
    selectedSpecimenId = rec.id
    localStorage.setItem SPECIMEN_KEY, selectedSpecimenId
    await renderSpecimens()
    log "Uncovered firmware fossil (" + fmtSize(rec.size) + ") — saved in this browser."
  catch err
    log "Couldn't save firmware fossil: " + err.message

selectSpecimen = (id) ->
  try
    recs = await idbAllSpecimens()
    rec = recs.find (r) -> r.id is id
    return unless rec?.blob?
    firmwareBytes = new Uint8Array rec.blob
    selectedSpecimenId = id
    localStorage.setItem SPECIMEN_KEY, id
    enableFirmware()
    await renderSpecimens()
    log "Selected firmware fossil (" + fmtSize(rec.size) + ") — ready to flash or download."
    setStatus "Firmware fossil loaded — ready to flash or download.", "done"
  catch err
    setStatus "Couldn't load firmware fossil: " + err.message, "fail"

downloadSpecimen = (rec) ->
  return unless rec?.blob?
  try
    bytes = new Uint8Array rec.blob
    blob = new Blob [bytes], type: "application/octet-stream"
    url = URL.createObjectURL blob
    a = document.createElement "a"
    a.href = url
    a.download = rec.name or "pteronautos-firmware.bin"
    document.body.appendChild a
    a.click()
    a.remove()
    URL.revokeObjectURL url
    log "Downloaded firmware fossil (" + fmtSize(bytes.length) + ")."
  catch err
    setStatus "Couldn't download firmware fossil: " + err.message, "fail"

dismissSpecimen = (id) ->
  return unless window.confirm "Dismiss this firmware fossil? It will be removed from this browser."
  await idbDeleteSpecimen id
  if selectedSpecimenId is id
    selectedSpecimenId = null
    localStorage.removeItem SPECIMEN_KEY
    firmwareBytes = null
    els.downloadBtn.disabled = true
    els.downloadGzBtn.disabled = true
    els.flashBtn.disabled = true if serialSupported
  await renderSpecimens()
  log "Dismissed firmware fossil."

clearSpecimens = ->
  return unless window.confirm "Dismiss all firmware fossils? This cannot be undone."
  await idbClearSpecimens()
  selectedSpecimenId = null
  localStorage.removeItem SPECIMEN_KEY
  firmwareBytes = null
  els.downloadBtn.disabled = true
  els.downloadGzBtn.disabled = true
  els.flashBtn.disabled = true if serialSupported
  await renderSpecimens()
  log "Dismissed all firmware fossils."

specimenNode = (rec) ->
  li = document.createElement "li"
  li.className = "flasher-specimen"
  li.classList.add "selected" if rec.id is selectedSpecimenId

  selectBtn = document.createElement "button"
  selectBtn.type = "button"
  selectBtn.className = "flasher-specimen-select"
  selectBtn.title = "Load this firmware fossil"

  name = document.createElement "span"
  name.className = "flasher-specimen-name"
  name.textContent = "Firmware fossil"

  meta = document.createElement "span"
  meta.className = "flasher-specimen-meta"
  meta.textContent = specimenMeta rec

  selectBtn.appendChild name
  selectBtn.appendChild meta
  selectBtn.addEventListener "click", -> selectSpecimen rec.id

  downloadBtn = document.createElement "button"
  downloadBtn.type = "button"
  downloadBtn.className = "flasher-specimen-download"
  downloadBtn.title = "Download this firmware fossil"
  downloadBtn.setAttribute "aria-label", "Download this firmware fossil"
  downloadBtn.textContent = "↓"
  downloadBtn.addEventListener "click", ->
    downloadSpecimen rec

  dismiss = document.createElement "button"
  dismiss.type = "button"
  dismiss.className = "flasher-specimen-dismiss"
  dismiss.title = "Dismiss"
  dismiss.setAttribute "aria-label", "Dismiss this firmware fossil"
  dismiss.textContent = "×"
  dismiss.addEventListener "click", -> dismissSpecimen rec.id

  li.appendChild selectBtn
  li.appendChild downloadBtn
  li.appendChild dismiss
  li

renderSpecimens = ->
  try
    recs = await idbAllSpecimens()
  catch
    return
  recs.sort (a, b) -> (b.ts or 0) - (a.ts or 0)
  els.specimensList.textContent = ""
  els.clearSpecimensBtn.disabled = recs.length is 0
  if recs.length is 0
    els.specimensList.classList.add "empty"
    li = document.createElement "li"
    li.className = "flasher-specimen-empty"
    li.textContent = els.specimensList.dataset.empty or "No firmware fossils uncovered yet."
    els.specimensList.appendChild li
    return
  els.specimensList.classList.remove "empty"
  for rec in recs
    els.specimensList.appendChild specimenNode rec

restoreSpecimens = ->
  if "indexedDB" of window
    try
      recs = await idbAllSpecimens()
      if recs.length
        stored = localStorage.getItem SPECIMEN_KEY
        target = recs.find((r) -> r.id is stored) or recs[0]
        if target?.blob?
          firmwareBytes = new Uint8Array target.blob
          selectedSpecimenId = target.id
          enableFirmware()
          log "Restored firmware fossil (" + fmtSize(target.size) + ") — ready to flash or download."
          setStatus "Firmware fossil loaded — ready to flash or download.", "done"
    catch
      # no storage or blocked — nothing to load
  renderSpecimens()

loadFromDisk = (file) ->
  buf = await file.arrayBuffer()
  firmwareBytes = new Uint8Array buf
  enableFirmware()
  log "Loaded " + file.name + " (" + firmwareBytes.length + " bytes) from disk."
  setStatus "Firmware image loaded — ready to flash or download.", "done"

closeSerial = ->
  t = activeTransport
  p = activePort
  activeTransport = null
  activePort = null
  try
    await t?.disconnect()
  catch
  try
    await p?.close()
  catch

disconnect = ->
  await closeSerial()
  els.disconnectBtn.classList.add "hidden"
  log "Disconnected."

startBuild = ->
  els.buildBtn.disabled = true
  els.flashBtn.disabled = true
  els.downloadBtn.disabled = true
  els.downloadGzBtn.disabled = true
  firmwareBytes = null
  currentRunId = null
  currentBuildInfo = null
  els.log.textContent = ""
  setStatus "Dispatching build…", "busy"
  els.runLink.classList.add "hidden"
  els.progressWrap.classList.add "hidden"
  busy = true

  try
    params = collectParams()
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
    sha = data.head_sha?.slice 0, 7
    branch = data.head_branch or "pteronautos"
    currentBuildInfo = { version: branch + (if sha then " @" + sha else "") }
    setStatus "Build complete — downloading image…", "busy"
    await downloadFirmware()
    await saveSpecimen()
    enableFirmware()
    setStatus "Build complete — firmware fossil saved. Ready to flash or download.", "done"
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
  ver = resp.headers.get "X-Pteronautos-Version"
  currentBuildInfo.version = ver if ver
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

# WiFi OTA image (.bin.gz). Gzip the in-memory .bin client-side (CompressionStream
# is present in every Web Serial-capable browser) — the resulting stream carries
# the ISIZE trailer that the ESP8266 eboot bootloader needs to inflate on boot.
# Kept separate from saveFirmware: this is for the RX WebUI updater (no recovery).
downloadGzFirmware = ->
  return unless firmwareBytes?
  els.downloadGzBtn.disabled = true
  try
    setStatus "Compressing WiFi image (.bin.gz)…", "busy"
    stream = new Blob([firmwareBytes]).stream().pipeThrough(new CompressionStream "gzip")
    blob = await new Response(stream).blob()
    url = URL.createObjectURL blob
    a = document.createElement "a"
    a.href = url
    a.download = "pteronautos-firmware.bin.gz"
    document.body.appendChild a
    a.click()
    a.remove()
    URL.revokeObjectURL url
    log "Saved pteronautos-firmware.bin.gz (" + blob.size + " bytes, WiFi / no-recovery)"
    setStatus "WiFi image downloaded — flash via the RX WebUI updater (advanced, no recovery).", "done"
  catch err
    setStatus "Compression failed: " + err.message, "fail"
  finally
    els.downloadGzBtn.disabled = false

terminal = ->
  clean: ->
    els.log.textContent = ""
  writeLine: (d) ->
    log d
  write: (d) ->
    els.log.textContent += d

# Fresh ESPLoader per connect attempt. esptool-js connect() opens the WebSerial
# port, then throws on a failed ROM sync WITHOUT closing it — so reusing one
# loader across retries hits "port is already open" on the second main() call.
# A new Transport on the same port + disconnect() on failure releases the port.
attemptConnect = (resetArg = null) ->
  transport = new Transport activePort, true
  loader = new ESPLoader
    transport: transport
    baudrate: 115200
    terminal: terminal()
    debugLogging: false
  try
    chip = await loader.main resetArg
    return { chip: chip, loader: loader, transport: transport }
  catch err
    try
      await transport.disconnect()
    catch
    throw err

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
    await closeSerial()
    activePort = await navigator.serial.requestPort()
    els.disconnectBtn.classList.remove "hidden"

    # Manual bootloader entry matches HARDWARE.md: hold BOOT (GPIO0 / CH1),
    # power-cycle, keep holding ~1 s, then release. Try a manual sync first
    # (no DTR/RTS disturbance), then fall back to an auto-reset attempt for
    # adapters that wire DTR/RTS to EN/GPIO0.
    log "Bootloader: hold BOOT (GPIO0 / CH1 button), power-cycle the board, keep holding ~1 s, then release."
    chip = null
    lastErr = null
    for attempt in [1..2]
      try
        conn = await attemptConnect "no_reset"
        chip = conn.chip
        esploader = conn.loader
        activeTransport = conn.transport
        log "Connected: " + chip
        break
      catch err
        lastErr = err
        if attempt is 1
          log "No bootloader response on manual sync — retrying…"
          await sleep 400
    unless chip?
      try
        conn = await attemptConnect()
        chip = conn.chip
        esploader = conn.loader
        activeTransport = conn.transport
        log "Connected (auto-reset): " + chip
      catch err
        lastErr = err
    unless chip?
      throw lastErr or new Error "Could not reach the ROM bootloader. Hold BOOT (GPIO0), power-cycle, and retry."

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

    mushin = $("#hardware_target").value is "mushin"
    if mushin
      log "Flashing complete. The receiver stays in the ROM bootloader — exit MEDITATION to boot the new firmware."
    else
      log "Flashing complete. The receiver stays in the ROM bootloader — power-cycle (re-plug without BOOT held) to boot the new firmware."
    await esploader.after "no_reset_stub"

    if mushin
      setStatus "Flashed successfully — exit MEDITATION (long-press BOOT / tap RESET) to boot.", "done"
    else
      setStatus "Flashed successfully — power-cycle to boot the new firmware.", "done"
    els.progressBar.style.width = "100%"
    await disconnect()
  catch err
    await closeSerial()
    setStatus "Flash failed: " + err.message, "fail"
    log err.stack or err.message
  finally
    els.flashBtn.disabled = false
    busy = false

syncGyroFields = ->
  mushin = $("#hardware_target").value is "mushin"
  localGyro = $("#hardware_target").value is "pwmp7"
  zeph = $("#zephyrus_enabled")
  zephRow = zeph.closest ".flasher-toggle"
  # Local MPU6050 gyro exists only on the PWMP7 board — the EP2 variants have
  # no free I2C pins (GPIO4/5 are radio), and MUSHIN owns gyro on the RP2040.
  unless localGyro
    zeph.checked = false
    zeph.disabled = true
    zephRow.classList.add "is-disabled"
  else
    zeph.disabled = false
    zephRow.classList.remove "is-disabled"
  enabled = zeph.checked and localGyro
  wrap = $("#zephyrus-fields")
  wrap.classList.toggle "is-greyed", not enabled
  for el in wrap.querySelectorAll "input, select"
    el.disabled = not enabled
  $("#zephyrus-hint").classList.toggle "hidden", enabled
  $("#mushin-note").classList.toggle "hidden", not mushin

restoreConfig()
syncGyroFields()
restoreSpecimens()

for id in FIELD_IDS
  $(id).addEventListener "input", saveConfig
  $(id).addEventListener "change", saveConfig

$("#hardware_target").addEventListener "change", syncGyroFields
$("#zephyrus_enabled").addEventListener "change", syncGyroFields

for l in LOCALES
  $("#locale-#{l}").addEventListener "change", saveConfig

unless serialSupported
  els.serialWarning.classList.remove "hidden"
  els.flashBtn.classList.add "hidden"

els.buildBtn.addEventListener "click", startBuild
els.loadBtn.addEventListener "click", -> els.fileInput.click()
els.disconnectBtn.addEventListener "click", disconnect
els.clearSpecimensBtn.addEventListener "click", clearSpecimens
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
els.downloadGzBtn.addEventListener "click", downloadGzFirmware