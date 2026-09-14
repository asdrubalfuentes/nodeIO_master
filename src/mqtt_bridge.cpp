#include "mqtt_bridge.h"
#include "master_config.h"
#include "lora_master.h"
#include "modbus_gw.h"
#include "net_master.h"
#include "fw_version.h"
#include "log.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Puente MQTT del gateway. Contrato: ORCHESTRATION/MQTT_BRIDGE.md + REGISTER_MAP.md SS7.

// ---- offsets del MAPA B (contrato SS4) dentro del espejo (Holding Regs) ----
enum {
  HB_LEVEL = 0, HB_FLOW = 1, HB_LVL_RAW = 2, HB_FLW_RAW = 3,
  HB_DAY_W0 = 4, HB_DAY_W1 = 5, HB_MON_W0 = 6, HB_MON_W1 = 7,
  HB_STATUS = 8, HB_ALARMS = 9, HB_RSSI = 10, HB_AGE = 11,
  HB_LINK = 12, HB_RDERR = 13, HB_ALARMS_LATCH = 14,
};
enum {
  G_MARK = 96, G_NST = 97, G_ONLINE = 98, G_ALARM_OR = 99,
  G_HB = 100, G_UP_W0 = 101, G_UP_W1 = 102, G_ORIGIN = 103,
  G_LOGIC = 104, G_CONTRACT = 105,
};

static const char *ALM_NAME[11] = {
  "NIVEL_ALTO", "NIVEL_BAJO", "MARCHA_SECO", "SIN_CAUDAL", "FALLA_PRESOSTATO",
  "SIN_VOLTAJE", "TAPA_ABIERTA", "SIN_LORA", "DATO_OBSOLETO",
  "ESCALA_INVALIDA", "SOBRE_RANGO",
};

// ---- estado ----
static WiFiClient       s_plain;
static WiFiClientSecure s_sec;
static PubSubClient     s_mqtt;

static bool     s_begun        = false;
static uint32_t s_lastTry      = 0;
static uint32_t s_lastFast     = 0;
static uint32_t s_lastSlow     = 0;
static char     s_base[64]     = "";   // aysafi/<site>/orq/
static char     s_clientId[40] = "";
static bool     s_wantRollcall = false, s_wantOta = false;
static uint32_t s_lastCmdHash  = 0, s_lastCmdMs = 0;

// ---- helpers ----
static uint32_t hash32(const char *s, unsigned n) {
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
  return h;
}
static void topicOf(char *out, size_t n, const char *suffix) {
  snprintf(out, n, "%s%s", s_base, suffix);
}
static uint32_t u32hi(uint16_t w0, uint16_t w1) { return ((uint32_t)w0 << 16) | w1; }

static void almArray(uint16_t bits, JsonArray arr) {
  for (int i = 0; i < 11; i++) if (bits & (1 << i)) arr.add(ALM_NAME[i]);
}
static void pub(const char *suffix, JsonDocument &d, bool retained) {
  char t[96]; topicOf(t, sizeof(t), suffix);
  char buf[1024];
  size_t nn = serializeJson(d, buf, sizeof(buf));
  if (nn && nn < sizeof(buf)) s_mqtt.publish(t, (const uint8_t *)buf, nn, retained);
}

// ---- publicadores ----
static void pubGwState() {
  JsonDocument d;
  d["ts"] = netEpoch(); d["site"] = mcfg.mqttSite; d["v"] = 1;
  d["online"] = true;
  d["ip"] = netStaIp();
  d["rssi"] = (int)WiFi.RSSI();
  d["fw"] = FW_SEMVER;
  d["uptime_s"] = (uint32_t)(millis() / 1000UL);
  d["hora_ok"] = netTimeOk();
  pub("gw/state", d, true);
}

static void pubNodes() {
  JsonDocument d;
  d["ts"] = netEpoch(); d["site"] = mcfg.mqttSite; d["v"] = 1;
  JsonArray a = d["nodos"].to<JsonArray>();
  for (int i = 0; i < MASTER_MAX_NODES; i++) {
    if (!mcfg.nodes[i].addr) continue;
    JsonObject o = a.add<JsonObject>();
    o["slot"] = i;
    o["addr"] = mcfg.nodes[i].addr;
    o["mac"] = mcfg.nodes[i].mac;
    o["nombre"] = mcfg.nodes[i].name;
    o["enabled"] = mcfg.nodes[i].enabled;
    o["online"] = snap[i].online;
    o["rssi"] = snap[i].rssi;
  }
  pub("nodes", d, true);
}

static void pubPlant() {
  if (!modbusTcpReady()) return;
  JsonDocument d;
  d["ts"] = netEpoch(); d["site"] = mcfg.mqttSite; d["v"] = 1;
  d["marca_ok"] = (modbusMirrorHreg(G_MARK) == 0x0B01);
  d["contrato_v"] = modbusMirrorHreg(G_CONTRACT);
  d["logica_v"] = modbusMirrorHreg(G_LOGIC);
  d["origen"] = modbusMirrorHreg(G_ORIGIN) ? "LOGO!" : "PLC-SIM";
  d["n_estaciones"] = modbusMirrorHreg(G_NST);
  d["online_bits"] = modbusMirrorHreg(G_ONLINE);
  d["alarma_general"] = modbusMirrorHreg(G_ALARM_OR) != 0;
  d["latido"] = modbusMirrorHreg(G_HB);
  d["uptime_s"] = u32hi(modbusMirrorHreg(G_UP_W0), modbusMirrorHreg(G_UP_W1));
  d["gw_fw"] = FW_SEMVER;
  pub("plant", d, true);
}

static void pubStation(uint8_t s) {
  if (!modbusTcpReady()) return;
  uint16_t b = s * 32;
  int16_t lvl = (int16_t)modbusMirrorHreg(b + HB_LEVEL);
  int16_t flw = (int16_t)modbusMirrorHreg(b + HB_FLOW);
  uint16_t st = modbusMirrorHreg(b + HB_STATUS);

  JsonDocument d;
  d["ts"] = netEpoch(); d["site"] = mcfg.mqttSite; d["v"] = 1; d["st"] = s;
  d["nivel"] = lvl / 100.0;
  d["nivel_raw"] = modbusMirrorHreg(b + HB_LVL_RAW);
  d["caudal"] = flw / 100.0;
  d["caudal_raw"] = modbusMirrorHreg(b + HB_FLW_RAW);
  // Escala x1000 desde CONTRACT_VERSION 3 (cambio de rumbo 2026-09: los
  // acumulados llegan del nodo remoto sin conversion en el LOGO!, ver
  // ORCHESTRATION/REGISTER_MAP.md).
  d["acum_dia_m3"] = u32hi(modbusMirrorHreg(b + HB_DAY_W0), modbusMirrorHreg(b + HB_DAY_W1)) / 1000.0;
  d["acum_mes_m3"] = u32hi(modbusMirrorHreg(b + HB_MON_W0), modbusMirrorHreg(b + HB_MON_W1)) / 1000.0;
  JsonObject e = d["estado"].to<JsonObject>();
  e["presostato"]  = (st & (1 << 0)) != 0;
  e["volt_local"]  = (st & (1 << 1)) != 0;
  e["tamper"]      = (st & (1 << 2)) != 0;
  e["sirena"]      = (st & (1 << 4)) != 0;
  e["enlace"]      = (st & (1 << 5)) != 0;
  e["en_alarma"]   = (st & (1 << 6)) != 0;
  e["sirena_auto"] = (st & (1 << 7)) != 0;
  almArray(modbusMirrorHreg(b + HB_ALARMS),       d["alarmas"].to<JsonArray>());
  almArray(modbusMirrorHreg(b + HB_ALARMS_LATCH), d["alarmas_latch"].to<JsonArray>());
  d["rssi"] = (int16_t)modbusMirrorHreg(b + HB_RSSI);
  d["edad_s"] = modbusMirrorHreg(b + HB_AGE);
  d["vinculo"] = modbusMirrorHreg(b + HB_LINK);
  d["rderr"] = modbusMirrorHreg(b + HB_RDERR);

  char sub[24]; snprintf(sub, sizeof(sub), "station/%u/data", s);
  pub(sub, d, false);
}

static void pubNodeRaw(int i) {
  if (!mcfg.nodes[i].addr) return;
  NodeSnapshot &s = snap[i];
  JsonDocument d;
  d["ts"] = netEpoch(); d["site"] = mcfg.mqttSite; d["v"] = 1;
  d["nodo"] = i;
  d["addr"] = mcfg.nodes[i].addr;
  d["mac"] = mcfg.nodes[i].mac;
  JsonArray ai = d["ai"].to<JsonArray>();
  for (int k = 0; k < 4; k++) ai.add(s.ai[k]);
  JsonArray di = d["di"].to<JsonArray>();
  for (int k = 0; k < 4; k++) di.add(s.di[k] ? 1 : 0);
  // ArduinoJson guarda const char* por puntero: el buffer debe seguir vivo hasta
  // serializar (al final de la funcion). Lo mantenemos aqui.
  static char robuf[4][2];
  JsonArray ro = d["ro"].to<JsonArray>();
  for (int k = 0; k < 4; k++) {
    robuf[k][0] = s.ro[k] ? s.ro[k] : '-';
    robuf[k][1] = 0;
    ro.add((const char *)robuf[k]);
  }
  d["enlace"] = s.online;
  d["rssi"] = s.rssi;
  d["edad_s"] = s.lastReplyMs ? (millis() - s.lastReplyMs) / 1000UL : 65535UL;

  char sub[24]; snprintf(sub, sizeof(sub), "node/%u/raw", mcfg.nodes[i].addr);
  pub(sub, d, false);
}

// ---- comandos ----
static void ackTo(const char *reqTopic, const char *req, bool ok, const char *detail) {
  JsonDocument d;
  d["ts"] = netEpoch();
  d["req"] = req;
  d["ok"] = ok;
  if (detail) d["detail"] = detail;
  char t[112]; snprintf(t, sizeof(t), "%s/ack", reqTopic);
  char buf[192]; size_t nn = serializeJson(d, buf, sizeof(buf));
  s_mqtt.publish(t, (const uint8_t *)buf, nn, false);
}

static void onStationCmd(const char *reqTopic, uint8_t s, JsonDocument &d) {
  if (s > 1) { ackTo(reqTopic, "?", false, "estacion fuera de rango"); return; }
  uint16_t base = MAPG_CMD_BASE + s * MAPG_CMD_STRIDE;

  const char *siren = d["siren"].is<const char *>() ? d["siren"].as<const char *>() : nullptr;
  if (siren) {
    if      (!strcmp(siren, "auto"))   { modbusCmdCoil(base + 1, true,  false); ackTo(reqTopic, "siren:auto",   true, nullptr); }
    else if (!strcmp(siren, "manual")) { modbusCmdCoil(base + 1, false, false); ackTo(reqTopic, "siren:manual", true, nullptr); }
    else if (!strcmp(siren, "on"))     { modbusCmdCoil(base + 0, true,  false); ackTo(reqTopic, "siren:on",     true, nullptr); }
    else if (!strcmp(siren, "off"))    { modbusCmdCoil(base + 0, false, false); ackTo(reqTopic, "siren:off",    true, nullptr); }
    else ackTo(reqTopic, "siren", false, "valor: auto|manual|on|off");
    return;
  }
  if (d["silence"].as<bool>()) { modbusCmdCoil(base + 2, true, true); ackTo(reqTopic, "silence", true, nullptr); return; }
  if (d["ack"].as<bool>())     { modbusCmdCoil(base + 5, true, true); ackTo(reqTopic, "ack",     true, nullptr); return; }

  const char *rst = d["reset"].is<const char *>() ? d["reset"].as<const char *>() : nullptr;
  if (rst) {
    if (!d["arm"].as<bool>()) { ackTo(reqTopic, "reset", false, "falta arm:true en el mismo mensaje"); return; }
    if      (!strcmp(rst, "day"))   { modbusCmdCoil(base + 9, true, true); modbusCmdCoil(base + 3, true, true); ackTo(reqTopic, "reset:day",   true, nullptr); }
    else if (!strcmp(rst, "month")) { modbusCmdCoil(base + 9, true, true); modbusCmdCoil(base + 4, true, true); ackTo(reqTopic, "reset:month", true, nullptr); }
    else ackTo(reqTopic, "reset", false, "valor: day|month");
    return;
  }
  if (!d["scale"].isNull()) { ackTo(reqTopic, "scale", false, "scale/set aun no implementado en el gateway"); return; }
  ackTo(reqTopic, "?", false, "payload sin comando reconocido");
}

static void onNodeRelayCmd(const char *reqTopic, uint8_t addr, JsonDocument &d) {
  int slot = masterFindByAddr(addr);
  if (slot < 0) { ackTo(reqTopic, "relay", false, "nodo desconocido"); return; }
  if (!snap[slot].online) { ackTo(reqTopic, "relay", false, "nodo offline"); return; }

  if (!d["pulse"].isNull()) {
    int idx = d["pulse"]["idx"] | 0;
    int ms  = d["pulse"]["ms"]  | (int)mcfg.pulseMs;
    if (idx < 1 || idx > 4 || ms < 1) { ackTo(reqTopic, "pulse", false, "idx 1..4, ms>0"); return; }
    masterQueuePulse(slot, (uint8_t)idx, (uint16_t)ms);
    ackTo(reqTopic, "pulse", true, nullptr);
    return;
  }
  JsonArray ro = d["ro"].as<JsonArray>();
  if (!ro.isNull()) {
    char want[4];
    for (int k = 0; k < 4; k++) {
      int v = (k < (int)ro.size()) ? (ro[k] | -1) : -1;
      want[k] = (v == 1) ? '1' : (v == 0) ? '0' : '-';
    }
    masterQueueRelays(slot, want);
    ackTo(reqTopic, "ro", true, nullptr);
    return;
  }
  ackTo(reqTopic, "relay", false, "usa {\"ro\":[..]} o {\"pulse\":{idx,ms}}");
}

static void onMessage(char *t, uint8_t *payload, unsigned len) {
  uint32_t h = hash32(t, strlen(t)) ^ hash32((const char *)payload, len);
  if (h == s_lastCmdHash && millis() - s_lastCmdMs < 2000) return;   // anti-rebote
  s_lastCmdHash = h; s_lastCmdMs = millis();

  size_t bl = strlen(s_base);
  if (strncmp(t, s_base, bl) != 0) return;
  const char *rest = t + bl;                 // p.ej. "station/0/cmd"

  JsonDocument d;
  if (deserializeJson(d, payload, len)) { ackTo(t, "?", false, "JSON invalido"); return; }

  if (!strncmp(rest, "station/", 8)) {
    uint8_t s = (uint8_t)atoi(rest + 8);
    const char *slash = strchr(rest + 8, '/');
    if (slash && !strcmp(slash, "/cmd")) { onStationCmd(t, s, d); return; }
  } else if (!strncmp(rest, "node/", 5)) {
    uint8_t addr = (uint8_t)atoi(rest + 5);
    const char *slash = strchr(rest + 5, '/');
    if (slash && !strcmp(slash, "/relay")) { onNodeRelayCmd(t, addr, d); return; }
  } else if (!strcmp(rest, "gw/cmd")) {
    bool any = false;
    if (d["rollcall"].as<bool>()) { s_wantRollcall = true; any = true; }
    if (d["ota"].as<bool>())      { s_wantOta = true;      any = true; }
    if (d["reboot"].as<bool>())   { ackTo(t, "reboot", true, "reiniciando"); delay(200); ESP.restart(); }
    ackTo(t, "gw", any, any ? "encolado" : "sin accion reconocida");
    return;
  }
  // cualquier otro topico (nuestra propia telemetria por el wildcard): ignorar
}

// ---- ciclo de vida ----
void mqttBridgeBegin() {
  if (!mcfg.mqttEnabled) { LOGLN("[mqtt] deshabilitado"); return; }
  snprintf(s_base, sizeof(s_base), "aysafi/%s/orq/", mcfg.mqttSite);
  String mac = masterMac();
  String tail = mac.length() >= 6 ? mac.substring(mac.length() - 6) : mac;
  snprintf(s_clientId, sizeof(s_clientId), "orq-%s-%s", mcfg.mqttSite, tail.c_str());

  if (mcfg.mqttTls) { s_sec.setInsecure(); s_mqtt.setClient(s_sec); }
  else              { s_mqtt.setClient(s_plain); }
  s_mqtt.setServer(mcfg.mqttHost, mcfg.mqttPort ? mcfg.mqttPort : (mcfg.mqttTls ? 8883 : 1883));
  s_mqtt.setBufferSize(1200);
  s_mqtt.setKeepAlive(30);
  s_mqtt.setSocketTimeout(4);   // cap del handshake: no frenar Modbus si el broker no responde
  s_mqtt.setCallback(onMessage);
  s_begun = true;
  LOGF("[mqtt] %s:%u  base=%s  id=%s\n", mcfg.mqttHost, mcfg.mqttPort, s_base, s_clientId);
}

static void subCmd(const char *suffix) {
  char t[96]; topicOf(t, sizeof(t), suffix);
  s_mqtt.subscribe(t, 1);
}

static void reconnect() {
  if (millis() - s_lastTry < 20000) return;   // reintento espaciado (el connect bloquea ~4 s)
  s_lastTry = millis();

  char will[96]; topicOf(will, sizeof(will), "gw/state");
  const char *u = mcfg.mqttUser[0] ? mcfg.mqttUser : nullptr;
  const char *p = mcfg.mqttPass[0] ? mcfg.mqttPass : nullptr;
  if (!s_mqtt.connect(s_clientId, u, p, will, 0, true, "{\"online\":false}")) {
    LOGF("[mqtt] connect fallo rc=%d\n", s_mqtt.state());
    return;
  }
  LOGLN("[mqtt] conectado");
  subCmd("station/+/cmd");
  subCmd("station/+/scale/set");
  subCmd("node/+/relay");
  subCmd("gw/cmd");
  pubGwState();
  pubNodes();
}

void mqttBridgeLoop() {
  if (!s_begun || !netStaUp()) return;

  if (!s_mqtt.connected()) { reconnect(); return; }
  s_mqtt.loop();

  uint32_t now = millis();
  uint16_t fast = mcfg.mqttPubMs ? mcfg.mqttPubMs : 2000;
  if (now - s_lastFast >= fast) {
    s_lastFast = now;
    pubPlant();
    for (uint8_t s = 0; s < 2; s++) pubStation(s);
    for (int i = 0; i < MASTER_MAX_NODES; i++) if (mcfg.nodes[i].addr) pubNodeRaw(i);
  }
  if (now - s_lastSlow >= 30000) {
    s_lastSlow = now;
    pubGwState();
    pubNodes();
  }
}

bool mqttBridgeConnected() { return s_begun && s_mqtt.connected(); }
bool mqttTakeWantRollcall() { bool v = s_wantRollcall; s_wantRollcall = false; return v; }
bool mqttTakeWantOta()      { bool v = s_wantOta;      s_wantOta = false;      return v; }
