import { ESPLoader, Transport } from "./vendor/esptool-js.js";

const $ = (sel) => document.querySelector(sel);

const els = {
  buildBtn: $("#build-btn"),
  flashBtn: $("#flash-btn"),
  buildHint: $("#build-hint"),
  statusCard: $("#status-card"),
  statusDot: $("#status-dot"),
  statusText: $("#status-text"),
  runLink: $("#run-link"),
  progressWrap: $("#progress-wrap"),
  progressBar: $("#progress-bar"),
  log: $("#log"),
};

let currentRunId = null;
let firmwareBytes = null;

function log(line) {
  els.log.textContent += line + "\n";
  els.log.scrollTop = els.log.scrollHeight;
}

function setStatus(text, state) {
  els.statusCard.classList.remove("hidden");
  els.statusText.textContent = text;
  els.statusDot.className = "dot";
  if (state === "done") els.statusDot.classList.add("done");
  if (state === "fail") els.statusDot.classList.add("fail");
}

function collectParams() {
  const num = (id) => parseInt($(id).value, 10);
  return {
    mixer_profile: $("#mixer_profile").value,
    regulatory_domain: $("#regulatory_domain").value,
    binding_phrase: $("#binding_phrase").value.trim(),
    auto_wifi_on_interval: num("#auto_wifi_on_interval"),
    zephyrus_i2c_sda: num("#zephyrus_i2c_sda"),
    zephyrus_i2c_scl: num("#zephyrus_i2c_scl"),
    zephyrus_board_rotation: $("#zephyrus_board_rotation").value,
    mushin_rx_pin: num("#mushin_rx_pin"),
    mushin_tx_pin: num("#mushin_tx_pin"),
    mushin_baud: num("#mushin_baud"),
  };
}

async function startBuild() {
  els.buildBtn.disabled = true;
  els.flashBtn.disabled = true;
  firmwareBytes = null;
  currentRunId = null;
  els.log.textContent = "";
  setStatus("Dispatching build…", "busy");
  els.runLink.classList.add("hidden");
  els.progressWrap.classList.add("hidden");

  try {
    const resp = await fetch("/api/build", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(collectParams()),
    });
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.error || `HTTP ${resp.status}`);

    currentRunId = data.run_id;
    if (data.html_url) {
      els.runLink.href = data.html_url;
      els.runLink.classList.remove("hidden");
    }
    setStatus("Build queued — waiting for GitHub Actions…", "busy");
    poll();
  } catch (err) {
    setStatus(`Build failed: ${err.message}`, "fail");
    els.buildBtn.disabled = false;
  }
}

async function poll() {
  if (!currentRunId) return;
  try {
    const resp = await fetch(`/api/build/${currentRunId}`);
    const data = await resp.json();
    if (!resp.ok) throw new Error(data.error || `HTTP ${resp.status}`);

    if (!data.ready) {
      setStatus(`Building… (${data.status || "queued"})`, "busy");
      setTimeout(poll, 5000);
      return;
    }

    if (data.conclusion !== "success") {
      setStatus(`Build ${data.conclusion} — check the GitHub run.`, "fail");
      els.buildBtn.disabled = false;
      return;
    }

    setStatus("Build complete — ready to flash.", "done");
    els.buildBtn.disabled = false;
    els.flashBtn.disabled = false;
  } catch (err) {
    setStatus(`Polling error: ${err.message}`, "fail");
    els.buildBtn.disabled = false;
  }
}

async function downloadFirmware() {
  setStatus("Downloading firmware…", "busy");
  const resp = await fetch(`/api/build/${currentRunId}/download`);
  if (!resp.ok) {
    const err = await resp.json().catch(() => ({ error: `HTTP ${resp.status}` }));
    throw new Error(err.error || `HTTP ${resp.status}`);
  }
  const buf = await resp.arrayBuffer();
  firmwareBytes = new Uint8Array(buf);
  log(`Firmware: ${firmwareBytes.length} bytes (merged image @ 0x0000)`);
}

function terminal() {
  return {
    clean() { els.log.textContent = ""; },
    writeLine(d) { log(d); },
    write(d) { els.log.textContent += d; },
  };
}

async function flash() {
  if (!("serial" in navigator)) {
    setStatus("Web Serial is not supported in this browser — use Chrome or Edge.", "fail");
    return;
  }

  els.flashBtn.disabled = true;
  els.progressWrap.classList.remove("hidden");
  els.progressBar.style.width = "0%";

  try {
    if (!firmwareBytes) await downloadFirmware();

    setStatus("Connecting to device…", "busy");
    const port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });

    const transport = new Transport(port, true);
    const esploader = new ESPLoader({
      transport,
      baudrate: 115200,
      terminal: terminal(),
      debugLogging: false,
    });

    const chip = await esploader.main();
    log(`Connected: ${chip}`);

    setStatus("Flashing…", "busy");
    await esploader.writeFlash({
      fileArray: [{ data: firmwareBytes, address: 0x0000 }],
      flashMode: "dout",
      flashFreq: "80m",
      flashSize: "1MB",
      eraseAll: false,
      compress: true,
      reportProgress: (_fileIndex, written, total) => {
        const pct = total > 0 ? Math.round((written / total) * 100) : 0;
        els.progressBar.style.width = `${pct}%`;
        els.statusText.textContent = `Flashing… ${pct}%`;
      },
    });

    log("Flashing complete. Resetting…");
    await esploader.after("hard_reset");

    setStatus("Flashed successfully.", "done");
    els.progressBar.style.width = "100%";
    await port.close();
  } catch (err) {
    setStatus(`Flash failed: ${err.message}`, "fail");
    log(err.stack || err.message);
  } finally {
    els.flashBtn.disabled = false;
  }
}

els.buildBtn.addEventListener("click", startBuild);
els.flashBtn.addEventListener("click", flash);
