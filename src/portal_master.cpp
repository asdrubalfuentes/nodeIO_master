#include "portal_master.h"
#include "master_config.h"
#include "lora_master.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

bool portalActive = false;

static DNSServer  dns;
static WebServer  web(80);
static const byte DNS_PORT = 53;

static const char* FMT_NAMES[6] = { "8N1", "8E1", "8O1", "8N2", "8E2", "8O2" };

// --------------------------------------------------------------------------
static String opt(int v, int cur, const char* label) {
  return "<option value=" + String(v) + (v == cur ? " selected" : "") + ">" + label + "</option>";
}

static String buildPage() {
  String h;
  h.reserve(6144);
  h += F("<!doctype html><html><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Master IO - Config</title><style>"
         "body{font-family:sans-serif;margin:0;background:#111;color:#eee}"
         "main{max-width:560px;margin:auto;padding:16px}"
         "h1{font-size:19px}h2{font-size:15px;color:#8cf;margin:18px 0 6px}"
         "fieldset{border:1px solid #444;margin:10px 0;border-radius:8px}"
         "legend{color:#8cf;padding:0 6px}label{display:block;margin:8px 0 2px;font-size:14px}"
         "input,select{width:100%;padding:8px;box-sizing:border-box;background:#222;color:#eee;"
         "border:1px solid #555;border-radius:6px}"
         ".row{display:flex;gap:10px}.row>div{flex:1}"
         "table{width:100%;border-collapse:collapse;font-size:13px}td,th{border:1px solid #444;padding:4px}"
         "button{padding:8px 12px;font-size:14px;background:#08f;color:#fff;border:0;border-radius:6px}"
         ".big{width:100%;padding:12px;margin-top:12px;font-size:16px}"
         ".del{background:#a33}</style></head><body><main>"
         "<h1>Master IO &mdash; Pasarela LoRa/Modbus</h1>");

  // ---- node table ----
  h += F("<h2>Nodos adoptados</h2><table><tr><th>#</th><th>Addr</th><th>Nombre</th><th>MAC</th>"
         "<th>Estado</th><th></th></tr>");
  for (int i = 0; i < MASTER_MAX_NODES; i++) {
    MasterNode& n = mcfg.nodes[i];
    if (!n.addr) continue;
    String st = !n.enabled ? "off" : (snap[i].online ? "ONLINE" : "offline");
    h += "<tr><td>" + String(i) + "</td><td>" + String(n.addr) + "</td><td>" + n.name +
         "</td><td>" + n.mac + "</td><td>" + st + "</td><td>"
         "<form method=post action=/toggle style=display:inline>"
         "<input type=hidden name=slot value=" + String(i) + ">"
         "<button>" + (n.enabled ? "Desact." : "Activar") + "</button></form> "
         "<form method=post action=/remove style=display:inline>"
         "<input type=hidden name=slot value=" + String(i) + ">"
         "<button class=del>Quitar</button></form></td></tr>";
  }
  h += "</table>";

  // ---- discovery ----
  h += F("<h2>Descubrir nodos</h2>"
         "<form method=post action=/scan><button class=big>Buscar nodos sin adoptar</button></form>");
  if (discoveredCount > 0) {
    h += "<table><tr><th>MAC</th><th>FW</th><th>Direcci&oacute;n</th><th>Nombre</th><th></th></tr>";
    for (uint8_t i = 0; i < discoveredCount; i++) {
      h += "<tr><td>" + String(discovered[i].mac) + "</td><td>" + String(discovered[i].fw) +
           "</td><form method=post action=/adopt>"
           "<input type=hidden name=mac value=" + String(discovered[i].mac) + ">"
           "<td><input name=addr type=number min=1 max=254 style=width:70px></td>"
           "<td><input name=name maxlength=15></td>"
           "<td><button>Agregar</button></td></form></tr>";
    }
    h += "</table>";
  } else {
    h += F("<p style=color:#999>Sin resultados todav&iacute;a. Pulsa \"Buscar\".</p>");
  }

  // ---- settings form ----
  h += F("<form method=post action=/save>"
         "<fieldset><legend>LoRa (canal autoritativo, se empuja al adoptar)</legend>");
  h += "<label>Direcci&oacute;n LoRa de la pasarela</label><input name=maddr type=number min=1 max=254 value=" + String(mcfg.masterLoraAddr) + ">";
  h += "<div class=row><div><label>Frecuencia MHz</label><input name=lfreq type=number step=0.1 value=" + String(mcfg.loraFreq, 1) + "></div>";
  h += "<div><label>BW kHz</label><input name=lbw type=number step=0.1 value=" + String(mcfg.loraBw, 1) + "></div></div>";
  h += "<div class=row><div><label>SF</label><input name=lsf type=number min=7 max=12 value=" + String(mcfg.loraSf) + "></div>";
  h += "<div><label>CR</label><input name=lcr type=number min=5 max=8 value=" + String(mcfg.loraCr) + "></div>";
  h += "<div><label>Sync</label><input name=lsync value=0x" + String(mcfg.loraSync, HEX) + "></div>";
  h += "<div><label>TX dBm</label><input name=lpwr type=number min=2 max=22 value=" + String(mcfg.loraPwr) + "></div></div>";
  h += "<div class=row><div><label>Sondeo ms</label><input name=pollms type=number min=50 max=60000 value=" + String(mcfg.pollMs) + "></div>";
  h += "<div><label>Timeout ACK ms</label><input name=ackms type=number min=50 max=10000 value=" + String(mcfg.ackTimeoutMs) + "></div>";
  h += "<div><label>Fallos->OFFLINE</label><input name=offa type=number min=1 max=20 value=" + String(mcfg.offlineAfter) + "></div>";
  h += "<div><label>Pulso ms (coil)</label><input name=pulms type=number min=10 max=60000 value=" + String(mcfg.pulseMs) + "></div></div>";
  h += F("</fieldset><fieldset><legend>Modbus RTU (RS-485)</legend>");
  h += "<div class=row><div><label>Slave ID</label><input name=mbid type=number min=1 max=247 value=" + String(mcfg.mbSlaveId) + "></div>";
  h += "<div><label>Baud</label><input name=mbbaud type=number value=" + String(mcfg.mbBaud) + "></div>";
  h += "<div><label>Formato</label><select name=mbfmt>";
  for (int f = 0; f < 6; f++) h += opt(f, mcfg.mbFormat, FMT_NAMES[f]);
  h += "</select></div></div>";
  h += "<div class=row><div><label>Pin RX</label><input name=mbrx type=number value=" + String(mcfg.mbRxPin) + "></div>";
  h += "<div><label>Pin TX</label><input name=mbtx type=number value=" + String(mcfg.mbTxPin) + "></div>";
  h += "<div><label>Pin DE (-1=none)</label><input name=mbde type=number value=" + String(mcfg.mbDePin) + "></div></div>";
  h += F("</fieldset><fieldset><legend>IO local</legend>"
         "<label><input type=checkbox name=locio style=width:auto");
  h += String(mcfg.localIoEnabled ? " checked" : "") +
       "> Leer entradas/reles del propio carrier (Modbus Ireg 904+, Coil 900+)";
  h += F("<p style=color:#c96>Aviso: por defecto RX/TX/DE de Modbus usan la regleta AI "
         "(GPIO2/3/4). Si activas IO local, mueve esos pines.</p></fieldset>");
  h += F("<fieldset><legend>WiFi del portal</legend>");
  h += "<label>SSID</label><input name=apssid maxlength=23 value='" + String(mcfg.apSsid) + "'>";
  h += "<label>Clave (min 8, vac&iacute;o = abierta)</label><input name=appass maxlength=23 value='" + String(mcfg.apPass) + "'>";
  h += F("</fieldset><button class=big type=submit>Guardar y reiniciar</button></form></main></body></html>");
  return h;
}

// --------------------------------------------------------------------------
static void handleRoot() { web.send(200, "text/html", buildPage()); }

static void redirectHome() {
  web.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  web.send(302, "text/plain", "");
}

static long argL(const char* k, long def) {
  return web.hasArg(k) ? web.arg(k).toInt() : def;
}

static void handleSave() {
  mcfg.masterLoraAddr = (uint8_t)constrain(argL("maddr", mcfg.masterLoraAddr), 1, 254);
  if (web.hasArg("lfreq")) mcfg.loraFreq = web.arg("lfreq").toFloat();
  if (web.hasArg("lbw"))   mcfg.loraBw   = web.arg("lbw").toFloat();
  mcfg.loraSf   = (uint8_t)constrain(argL("lsf", mcfg.loraSf), 7, 12);
  mcfg.loraCr   = (uint8_t)constrain(argL("lcr", mcfg.loraCr), 5, 8);
  mcfg.loraPwr  = (int8_t)constrain(argL("lpwr", mcfg.loraPwr), 2, 22);
  if (web.hasArg("lsync")) mcfg.loraSync = (uint8_t)strtol(web.arg("lsync").c_str(), nullptr, 0);
  mcfg.pollMs       = (uint16_t)constrain(argL("pollms", mcfg.pollMs), 50, 60000);
  mcfg.ackTimeoutMs = (uint16_t)constrain(argL("ackms", mcfg.ackTimeoutMs), 50, 10000);
  mcfg.offlineAfter = (uint8_t)constrain(argL("offa", mcfg.offlineAfter), 1, 20);
  mcfg.pulseMs      = (uint16_t)constrain(argL("pulms", mcfg.pulseMs), 10, 60000);

  mcfg.mbSlaveId = (uint8_t)constrain(argL("mbid", mcfg.mbSlaveId), 1, 247);
  mcfg.mbBaud    = (uint32_t)argL("mbbaud", mcfg.mbBaud);
  mcfg.mbFormat  = (uint8_t)constrain(argL("mbfmt", mcfg.mbFormat), 0, 5);
  mcfg.mbRxPin   = (int8_t)argL("mbrx", mcfg.mbRxPin);
  mcfg.mbTxPin   = (int8_t)argL("mbtx", mcfg.mbTxPin);
  mcfg.mbDePin   = (int8_t)argL("mbde", mcfg.mbDePin);

  mcfg.localIoEnabled = web.hasArg("locio");
  web.arg("apssid").toCharArray(mcfg.apSsid, sizeof(mcfg.apSsid));
  web.arg("appass").toCharArray(mcfg.apPass, sizeof(mcfg.apPass));

  bool ok = masterConfigSave();
  web.send(200, "text/html",
           String(F("<!doctype html><meta charset=utf-8><body style='font-family:sans-serif'>")) +
           (ok ? F("Guardado. Reiniciando...") : F("ERROR al guardar.")) + F("</body>"));
  delay(600);
  ESP.restart();
}

static void handleScan() {
  uint8_t n = masterDiscover();
  Serial.printf("[portal] discover -> %u nodo(s)\n", n);
  redirectHome();
}

static void handleAdopt() {
  String mac  = web.arg("mac");
  int    addr = web.arg("addr").toInt();
  String name = web.arg("name");
  bool ok = (mac.length() == 12) && masterAdopt(mac.c_str(), (uint8_t)addr, name.c_str());
  web.send(200, "text/html",
           String(F("<!doctype html><meta charset=utf-8><body style='font-family:sans-serif'>")) +
           (ok ? F("Nodo adoptado.") : F("Fallo la adopci&oacute;n (direccion en uso o sin ACK).")) +
           F("<br><a href=/>volver</a></body>"));
}

static void handleRemove() {
  masterRelease((int)web.arg("slot").toInt());
  redirectHome();
}

static void handleToggle() {
  int slot = web.arg("slot").toInt();
  if (slot >= 0 && slot < MASTER_MAX_NODES && mcfg.nodes[slot].addr) {
    mcfg.nodes[slot].enabled = !mcfg.nodes[slot].enabled;
    masterConfigSave();
  }
  redirectHome();
}

// --------------------------------------------------------------------------
void portalStart() {
  WiFi.mode(WIFI_AP);
  if (strlen(mcfg.apPass) >= 8) WiFi.softAP(mcfg.apSsid, mcfg.apPass);
  else                          WiFi.softAP(mcfg.apSsid);

  IPAddress ip = WiFi.softAPIP();
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(DNS_PORT, "*", ip);

  web.on("/", handleRoot);
  web.on("/save",   HTTP_POST, handleSave);
  web.on("/scan",   HTTP_POST, handleScan);
  web.on("/adopt",  HTTP_POST, handleAdopt);
  web.on("/remove", HTTP_POST, handleRemove);
  web.on("/toggle", HTTP_POST, handleToggle);
  web.on("/generate_204", redirectHome);
  web.on("/gen_204", redirectHome);
  web.on("/hotspot-detect.html", redirectHome);
  web.on("/ncsi.txt", redirectHome);
  web.on("/connecttest.txt", redirectHome);
  web.onNotFound(redirectHome);
  web.begin();

  portalActive = true;
  Serial.printf("[portal] AP '%s' en %s\n", mcfg.apSsid, ip.toString().c_str());
}

void portalStop() {
  web.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  portalActive = false;
}

void portalLoop() {
  dns.processNextRequest();
  web.handleClient();
}

String portalIP() { return WiFi.softAPIP().toString(); }
