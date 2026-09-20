# PteronautOS Cloud Build & Flasher — browser client (CoffeeScript).
# Drives GitHub Actions directly from the browser (no server) and flashes over
# Web Serial with esptool-js. A fine-grained GitHub token is stored in this
# browser's localStorage and used to trigger + download the build.

import { ESPLoader, Transport } from "./vendor/esptool-js.js"
import { unzipSync } from "./vendor/fflate.js"

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
  "#github_token"
  "#github_repo"
  "#github_ref"
]
LOCALES = ["pt", "en", "ru", "es", "de", "ko", "ja", "zh", "ar", "hi", "fr"]

WORKFLOW_NAME = "PteronautOS Cloud Build"
ARTIFACT_NAME = "pteronautos-firmware"

sleep = (ms) -> new Promise (r) -> setTimeout r, ms

repoValue = -> ($("#github_repo").value or "dantiel/PteronautOS").trim()
refValue = -> ($("#github_ref").value or "master").trim()

authHeaders = ->
  token = ($("#github_token").value or "").trim()
  {
    "Authorization": "Bearer " + token
    "Accept": "application/vnd.github+json"
    "User-Agent": "pteronautos-flasher"
  }

ghFetch = (path, init = {}) ->
  init.headers = Object.assign (init.headers or {}), authHeaders()
  resp = await fetch "https://api.github.com" + path, init
  return null if resp.status is 204
  text = await resp.text()
  data = null
  try
    data = JSON.parse text
  catch
    data = null
  unless resp.ok
    throw new Error (data?.message or data?.error or text or "HTTP " + resp.status)
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

num = (id) -> parseInt $(id).value, 10

collectParams = ->
  checked = LOCALES.filter (l) -> $("#locale-#{l}").checked
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
  i18n_locales: checked.join(",")

dispatchBuild = ->
  repo = repoValue()
  ref = refValue()
  await ghFetch "/repos/#{repo}/actions/workflows/cloud-build.yml/dispatches",
    method: "POST"
    headers: {"Content-Type": "application/json"}
    body: JSON.stringify { ref: ref, inputs: collectParams() }

findNewestRun = (repo, dispatchTime) ->
  for i in [0..30]
    data = await ghFetch "/repos/#{repo}/actions/runs?event=workflow_dispatch&per_page=10"
    run = (data.workflow_runs or [])
      .filter((r) -> r.name is WORKFLOW_NAME)
      .filter((r) -> new Date(r.created_at).getTime() >= dispatchTime - 5000)
      .sort((a, b) -> b.run_number - a.run_number)[0]
    return run if run
    await sleep 1000
  null

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
    token = ($("#github_token").value or "").trim()
    throw new Error "Enter a GitHub token first (see hint above)." unless token
    repo = repoValue()
    dispatchTime = Date.now()
    await dispatchBuild()
    run = await findNewestRun repo, dispatchTime
    unless run
      throw new Error "Build queued but the run was not found — check repo/branch and that the token has Actions access."
    currentRunId = run.id
    els.runLink.href = run.html_url
    els.runLink.classList.remove "hidden"
    setStatus "Build queued — waiting for GitHub Actions…", "busy"
    poll()
  catch err
    setStatus "Build failed: " + err.message, "fail"
    els.buildBtn.disabled = false

poll = ->
  return unless currentRunId
  try
    repo = repoValue()
    data = await ghFetch "/repos/#{repo}/actions/runs/" + currentRunId
    unless data.status is "completed"
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
  repo = repoValue()
  data = await ghFetch "/repos/#{repo}/actions/runs/" + currentRunId + "/artifacts"
  art = (data.artifacts or []).find((a) -> a.name is ARTIFACT_NAME) or (data.artifacts or [])[0]
  unless art?.archive_download_url
    throw new Error "No firmware artifact found for this run."
  resp = await fetch art.archive_download_url, { headers: authHeaders() }
  unless resp.ok
    throw new Error "Artifact download failed (HTTP " + resp.status + ")."
  buf = await resp.arrayBuffer()
  files = unzipSync new Uint8Array buf
  binName = Object.keys(files).find (n) -> n.endsWith ".bin"
  unless binName
    throw new Error "No .bin inside the artifact zip."
  firmwareBytes = files[binName]
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

for l in LOCALES
  $("#locale-#{l}").addEventListener "change", saveConfig

els.buildBtn.addEventListener "click", startBuild
els.flashBtn.addEventListener "click", flash