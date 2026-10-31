// Pteronaut-Egg — minimal WiFi bootstrap for the ESP8285 (1MB).
//
// Purpose: accept a full PteronautOS firmware image over WiFi and stage it
// with ESP8266 Updater (U_FLASH). It is deliberately tiny so that:
//   * the full (~636 KB) image fits in ESP.getFreeSketchSpace() when the egg
//     is running, and
//   * the full image's own WebUI can flash the egg back (~320 KB), closing
//     the egg<->full cycle indefinitely.
//
// No CRSF, no ArduinoJson, no littlefs, no AsyncWebServer — just AP + mDNS +
// raw TCP + the core Updater class.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Updater.h>

// ── AP identity ────────────────────────────────────────────────────────────
static const char *AP_SSID = "PTERONAUT-EGG";
static const char *AP_PASS = "hatchnatural"; // 8..63 chars, or "" for open AP
static const char *AP_HOST = "pteronaut-egg"; // -> http://pteronaut-egg.local

static WiFiServer gServer(80);

// ── Tiny embedded upload page (single string, no external assets) ─────────
// The ASCII egg is the drop-zone AND the click target. It swaps between the
// intact egg (idle / drag-over) and the hatched pterosaur (done).
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width">
<title>Pteronaut Egg</title>
<style>
body{font-family:system-ui,sans-serif;background:#0d1117;color:#d8e0e8;display:grid;place-items:center;min-height:100vh;margin:0}
main{text-align:center;padding:1.5em}
.egg{display:inline-block;cursor:pointer;border-radius:16px;padding:1em;transition:box-shadow .15s,transform .15s}
.egg:hover{transform:translateY(-2px)}
.egg.drag{box-shadow:0 0 0 3px #58a6ff,0 0 34px #58a6ff;transform:scale(1.04)}
.egg.drag pre{color:#cfe3ff}
pre#art{font:13px/1.15 ui-monospace,Menlo,Consolas,monospace;color:#9ecbff;margin:0;white-space:pre;text-align:left}
.visually-hidden{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}
code{background:#1b222c;padding:.15em .4em;border-radius:4px}
#st{white-space:pre-wrap;font-size:.85em;color:#8b98a5;min-height:2.5em;margin-top:1em}
</style>
<main>
  <label class="egg" id="egg" for="f">
    <pre id="art"></pre>
    <input type="file" id="f" class="visually-hidden">
  </label>
  <pre id="st">http://pteronaut-egg.local — drop firmware.bin onto the egg</pre>
</main>
<script>
const art=document.getElementById('art'),st=document.getElementById('st'),
      egg=document.getElementById('egg'),f=document.getElementById('f');
const ART_IDLE=String.raw`           .--''''--.
         .'          '.
        /              \
       /                \
      /                  \
     .                    .
     |                    |
     |                    |
     |                    |
     |                    |
     |                    |
     |                    |
     |                    |
     |                    |
     |                    |
     '                    '
      \                  /
   \   \                /   /
  \\\\  .'            '.  ////
^^^^^^^^'---____---'^^^^^^^^^^
  ,,.,,,..,,.,.,,,,....,,..,`;
const ART_DRAG=ART_IDLE;
const ART_DONE=String.raw`⠀⠀⠀⠀⠀⣠⣤⣤⣤⡀⠀⠐⢶⣤⣤⣀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⢀⣴⣿⣿⣿⣿⣿⣿⣦⠀⠀⠈⠛⠿⣿⣷⣶⣿⣷⣶⡶⠶⠒⠂⠀⠀⠀⠀
⠀⢀⡾⠿⠿⠿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠉⢿⣿⣯⣤⡶⠖⠋⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠙⣿⣿⣿⣦⠀⠀⠀⠀⣠⣶⣄⠉⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠘⣿⣿⣿⣿⣦⣤⣾⣿⣿⠇⠀⠀⣀⣀⣤⣴⣶⣦⡀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⢹⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⡀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢿⣿⣿⣿⣿⣿⣿⢻⣿⣿⡿⠟⠛⠛⠿⢿⣿⣿⡇⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣩⡍⠉⠙⠋⠰⠟⠋⠁⠀⠀⠀⠀⠀⠀⠈⠻⡇⠀
⠀⠀⠀⠀⠀⠀⠀⠠⣄⣠⡾⠛⢀⣼⠏⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠁⠀
⠀⠀⠀⠀⠀⠀⠀⠒⢛⠿⢴⣶⡟⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⠈⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀`;
art.textContent=ART_IDLE;

function setArt(s,cls){art.textContent=s;egg.classList.toggle('drag',cls);}
['dragenter','dragover'].forEach(ev=>egg.addEventListener(ev,e=>{e.preventDefault();e.dataTransfer.dropEffect='copy';setArt(ART_DRAG,true);}));
egg.addEventListener('dragleave',e=>{e.preventDefault();setArt(ART_IDLE,false);});
egg.addEventListener('drop',e=>{e.preventDefault();const x=e.dataTransfer.files[0];if(x)upload(x);});
f.addEventListener('change',()=>{if(f.files[0])upload(f.files[0]);});

async function upload(x){
  setArt(ART_DRAG,true);st.textContent='uploading '+x.size+' B …';
  try{
    const r=await fetch('/update',{method:'POST',body:x});
    if(r.ok){setArt(ART_DONE,false);st.textContent='flashing — the egg hatches, wait for reboot';}
    else{setArt(ART_IDLE,false);st.textContent='HTTP '+r.status+' — FAILED: '+await r.text();}
  }catch(e){setArt(ART_IDLE,false);st.textContent='ERR '+e;}
}
</script>
)rawliteral";

// ── Minimal HTTP handling ──────────────────────────────────────────────────
// Reads the request head, then either serves the page (GET) or streams the
// body into Updater (POST /update). One client at a time; bounded timeouts.

static bool readRequestHead(WiFiClient &c, bool &isPost, long &contentLen) {
  String req;
  unsigned long t = millis();
  while (c.connected()) {
    if (!c.available()) {
      if (millis() - t > 8000) return false; // no data → give up
      delay(1);
      continue;
    }
    String line = c.readStringUntil('\n');
    line.trim();
    if (req.length() == 0) {
      req = line;
      isPost = line.startsWith("POST");
    } else if (line.startsWith("Content-Length:")) {
      contentLen = line.substring(15).toInt();
    }
    if (line.length() == 0) return true; // blank line = end of headers
    t = millis();
  }
  return false;
}

static void servePage(WiFiClient &c) {
  c.print(F("HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"));
  c.print(FPSTR(PAGE_HTML));
  c.stop();
}

static void serveUpdate(WiFiClient &c, long contentLen) {
  if (contentLen <= 0) {
    c.print(F("HTTP/1.1 411 Length Required\r\nConnection: close\r\n\r\n"
              "missing Content-Length"));
    c.stop();
    return;
  }

  Serial.printf("flash: begin %ld B\n", (long)contentLen);
  if (!Update.begin((size_t)contentLen, U_FLASH)) {
    int e = Update.getError();
    c.print(F("HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n"));
    c.printf("Update.begin failed (err %d), free=%u", e, ESP.getFreeSketchSpace());
    c.stop();
    return;
  }

  // Stream the body in 1 KB chunks. `Update.end()` with no argument resets
  // (rather than finalizes) when the byte count is short, so calling it on any
  // abort path is the safe "cancel" — the egg stays the egg.
  static uint8_t buf[1024];
  long remaining = contentLen;
  unsigned long t = millis();
  while (remaining > 0 && c.connected()) {
    if (!c.available()) {
      if (millis() - t > 20000) { Update.end(); c.stop(); return; }
      delay(1);
      continue;
    }
    size_t chunk = c.read(buf, (size_t)(remaining < (long)sizeof(buf)
                                        ? remaining : (long)sizeof(buf)));
    if (chunk == 0) {
      if (millis() - t > 20000) { Update.end(); c.stop(); return; }
      delay(1);
      continue;
    }
    if (Update.write(buf, chunk) != chunk) { Update.end(); c.stop(); return; }
    remaining -= chunk;
    t = millis();
  }

  if (remaining != 0) {
    Update.end(); // incomplete → resets, does not reboot
    c.stop();
    return;
  }

  if (!Update.end() || !Update.isFinished()) {
    int e = Update.getError();
    c.print(F("HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n"));
    c.printf("Update failed (err %d)", e);
    c.stop();
    return;
  }

  c.print(F("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nok, rebooting"));
  c.stop();
  Serial.println("flash: ok, rebooting");
  delay(200);
  ESP.restart();
}

static void loopServer() {
  WiFiClient c = gServer.accept();
  if (!c) return;

  bool isPost = false;
  long contentLen = 0;
  if (!readRequestHead(c, isPost, contentLen)) { c.stop(); return; }

  if (isPost) serveUpdate(c, contentLen);
  else servePage(c);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nPteronaut-Egg boot");

  // Soft-AP; channel 1; no DHCP surprises — fixed AP IP 192.168.4.1.
  WiFi.mode(WIFI_AP);
  if (AP_PASS[0]) {
    WiFi.softAP(AP_SSID, AP_PASS);
  } else {
    WiFi.softAP(AP_SSID);
  }
  delay(100);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));

  // Human-readable address: http://pteronaut-egg.local (Bonjour/mDNS).
  if (MDNS.begin(AP_HOST)) {
    Serial.printf("mDNS: http://%s.local\n", AP_HOST);
  } else {
    Serial.println("mDNS: failed, use IP fallback");
  }

  gServer.begin();

  Serial.printf("egg AP: %s  http://%s.local  (http://192.168.4.1)\n",
                AP_SSID, AP_HOST);
  Serial.printf("free sketch space: %u B (%.1f KB)\n",
                ESP.getFreeSketchSpace(), ESP.getFreeSketchSpace() / 1024.0f);
}

void loop() {
  MDNS.update();
  loopServer();
}
