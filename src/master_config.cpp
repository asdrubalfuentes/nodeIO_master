#include "master_config.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <string.h>

MasterConfig mcfg;

static const char*    NVS_NS    = "masterio";
static const uint32_t CFG_MAGIC = 0xA75A3E04;   // bump on struct layout change
                                                // 03: + mbTransport/mbTcpPort + WiFi STA (Modbus TCP)
                                                // 04: + puente MQTT.  Desde aqui la identidad
                                                //     (tabla de nodos, canal LoRa, WiFi STA, MQTT)
                                                //     va en claves sueltas: un bump de magic ya
                                                //     NO borra el emparejamiento.

static void cpstr(char* d, const char* s, size_t n) {
  strncpy(d, s ? s : "", n - 1);
  d[n - 1] = 0;
}

// ---------------------------------------------------------------------------
// Identidad / emparejamiento: se guarda TAMBIEN como claves sueltas (sin magic).
// Sobrevive a un cambio de CFG_MAGIC (nuevo firmware). Cubre lo que dolería
// re-teclear en sitio: tabla de nodos, canal LoRa, direccion del master, WiFi de
// planta, transporte Modbus y la config del puente MQTT.
// ---------------------------------------------------------------------------
static void identityLoad(Preferences& p) {
  mcfg.masterLoraAddr = p.getUChar("id_maddr", mcfg.masterLoraAddr);
  mcfg.loraFreq = p.getFloat("id_lfreq", mcfg.loraFreq);
  mcfg.loraBw   = p.getFloat("id_lbw",   mcfg.loraBw);
  mcfg.loraSf   = p.getUChar("id_lsf",   mcfg.loraSf);
  mcfg.loraCr   = p.getUChar("id_lcr",   mcfg.loraCr);
  mcfg.loraSync = p.getUChar("id_lsync", mcfg.loraSync);
  mcfg.loraPwr  = (int8_t)p.getChar("id_lpwr", mcfg.loraPwr);

  mcfg.staEnabled = p.getBool("id_staen", mcfg.staEnabled);
  { String s = p.getString("id_ssid", mcfg.staSsid); cpstr(mcfg.staSsid, s.c_str(), sizeof(mcfg.staSsid)); }
  { String s = p.getString("id_spass", mcfg.staPass); cpstr(mcfg.staPass, s.c_str(), sizeof(mcfg.staPass)); }
  mcfg.staStatic = p.getBool("id_stast", mcfg.staStatic);
  mcfg.staIp   = p.getUInt("id_staip", mcfg.staIp);
  mcfg.staGw   = p.getUInt("id_stagw", mcfg.staGw);
  mcfg.staMask = p.getUInt("id_stamk", mcfg.staMask);

  mcfg.mbTransport = p.getUChar("id_mbtr", mcfg.mbTransport);
  mcfg.mbTcpPort   = p.getUShort("id_mbpt", mcfg.mbTcpPort);

  mcfg.mqttEnabled = p.getBool("id_mqen", mcfg.mqttEnabled);
  { String s = p.getString("id_mqhost", mcfg.mqttHost); cpstr(mcfg.mqttHost, s.c_str(), sizeof(mcfg.mqttHost)); }
  mcfg.mqttPort = p.getUShort("id_mqport", mcfg.mqttPort);
  mcfg.mqttTls  = p.getBool("id_mqtls", mcfg.mqttTls);
  { String s = p.getString("id_mquser", mcfg.mqttUser); cpstr(mcfg.mqttUser, s.c_str(), sizeof(mcfg.mqttUser)); }
  { String s = p.getString("id_mqpass", mcfg.mqttPass); cpstr(mcfg.mqttPass, s.c_str(), sizeof(mcfg.mqttPass)); }
  { String s = p.getString("id_mqsite", mcfg.mqttSite); cpstr(mcfg.mqttSite, s.c_str(), sizeof(mcfg.mqttSite)); }
  mcfg.mqttPubMs = p.getUShort("id_mqpub", mcfg.mqttPubMs);

  if (p.getUChar("id_nsz", 0) == (uint8_t)sizeof(MasterNode) &&
      p.getBytesLength("id_nodes") == sizeof(mcfg.nodes)) {
    p.getBytes("id_nodes", mcfg.nodes, sizeof(mcfg.nodes));
    mcfg.nodeCount = p.getUChar("id_ncnt", mcfg.nodeCount);
  }
}

static void identitySave(Preferences& p) {
  p.putUChar("id_maddr", mcfg.masterLoraAddr);
  p.putFloat("id_lfreq", mcfg.loraFreq);
  p.putFloat("id_lbw",   mcfg.loraBw);
  p.putUChar("id_lsf",   mcfg.loraSf);
  p.putUChar("id_lcr",   mcfg.loraCr);
  p.putUChar("id_lsync", mcfg.loraSync);
  p.putChar ("id_lpwr",  mcfg.loraPwr);

  p.putBool  ("id_staen", mcfg.staEnabled);
  p.putString("id_ssid",  mcfg.staSsid);
  p.putString("id_spass", mcfg.staPass);
  p.putBool  ("id_stast", mcfg.staStatic);
  p.putUInt  ("id_staip", mcfg.staIp);
  p.putUInt  ("id_stagw", mcfg.staGw);
  p.putUInt  ("id_stamk", mcfg.staMask);

  p.putUChar ("id_mbtr", mcfg.mbTransport);
  p.putUShort("id_mbpt", mcfg.mbTcpPort);

  p.putBool  ("id_mqen",   mcfg.mqttEnabled);
  p.putString("id_mqhost", mcfg.mqttHost);
  p.putUShort("id_mqport", mcfg.mqttPort);
  p.putBool  ("id_mqtls",  mcfg.mqttTls);
  p.putString("id_mquser", mcfg.mqttUser);
  p.putString("id_mqpass", mcfg.mqttPass);
  p.putString("id_mqsite", mcfg.mqttSite);
  p.putUShort("id_mqpub",  mcfg.mqttPubMs);

  p.putUChar ("id_nsz",   (uint8_t)sizeof(MasterNode));
  p.putBytes ("id_nodes", mcfg.nodes, sizeof(mcfg.nodes));
  p.putUChar ("id_ncnt",  mcfg.nodeCount);
  p.putBool  ("id_set",   true);
}

// ---------------------------------------------------------------------------
// Red de seguridad: espejo de la identidad "de campo" (tabla + canal + master
// addr) en LittleFS (particion aparte, intacta ante OTA y ante el magic).
// Se restaura solo si la identidad de NVS quedo en blanco.
// ---------------------------------------------------------------------------
struct IdMirror {
  uint32_t magic;         // 'AYID'
  uint8_t  ver, nodeSz, nodeCount, masterAddr;
  float    lFreq, lBw;
  uint8_t  lSf, lCr, lSync;
  int8_t   lPwr;
  MasterNode nodes[MASTER_MAX_NODES];
};
static const uint32_t IDM_MAGIC = 0x41594944;  // "AYID"
static const char*    IDM_PATH  = "/id.bin";
static bool s_lfsOk = false;

static void lfsEnsure() {
  if (!s_lfsOk) s_lfsOk = LittleFS.begin(true);
}

static void idMirrorWrite() {
  lfsEnsure();
  if (!s_lfsOk) return;
  IdMirror m{};
  m.magic = IDM_MAGIC; m.ver = 1; m.nodeSz = sizeof(MasterNode);
  m.nodeCount = mcfg.nodeCount; m.masterAddr = mcfg.masterLoraAddr;
  m.lFreq = mcfg.loraFreq; m.lBw = mcfg.loraBw;
  m.lSf = mcfg.loraSf; m.lCr = mcfg.loraCr; m.lSync = mcfg.loraSync; m.lPwr = mcfg.loraPwr;
  memcpy(m.nodes, mcfg.nodes, sizeof(m.nodes));
  File f = LittleFS.open(IDM_PATH, "w");
  if (!f) return;
  f.write((const uint8_t*)&m, sizeof(m));
  f.close();
}

// true si restauro algo
static bool idMirrorRestore() {
  lfsEnsure();
  if (!s_lfsOk || !LittleFS.exists(IDM_PATH)) return false;
  File f = LittleFS.open(IDM_PATH, "r");
  if (!f) return false;
  IdMirror m{};
  size_t n = f.read((uint8_t*)&m, sizeof(m));
  f.close();
  if (n != sizeof(m) || m.magic != IDM_MAGIC || m.nodeSz != sizeof(MasterNode)) return false;
  mcfg.masterLoraAddr = m.masterAddr;
  mcfg.loraFreq = m.lFreq; mcfg.loraBw = m.lBw;
  mcfg.loraSf = m.lSf; mcfg.loraCr = m.lCr; mcfg.loraSync = m.lSync; mcfg.loraPwr = m.lPwr;
  memcpy(mcfg.nodes, m.nodes, sizeof(mcfg.nodes));
  mcfg.nodeCount = m.nodeCount;
  return true;
}

void masterConfigFactory() {
  mcfg = MasterConfig{};
  mcfg.masterLoraAddr = 200;

  mcfg.mbTransport = MBT_TCP;      // por defecto Modbus TCP (el LOGO! 9 no tiene serie)
  mcfg.mbTcpPort   = 502;

  mcfg.mbSlaveId = 1;
  mcfg.mbBaud    = 19200;
  mcfg.mbFormat  = 1;              // 8E1
  mcfg.mbUsb     = true;           // bench default: Modbus over USB UART0 (GPIO43/44)
  mcfg.mbRxPin   = 44;            // U0RXD  (RS-485 carrier default was 2/3/4)
  mcfg.mbTxPin   = 43;            // U0TXD
  mcfg.mbDePin   = -1;           // direct link, no RS-485 direction control

  mcfg.staEnabled = false;
  mcfg.staSsid[0] = '\0';
  mcfg.staPass[0] = '\0';
  mcfg.staStatic  = false;
  mcfg.staIp = mcfg.staGw = mcfg.staMask = 0;

  mcfg.mqttEnabled = false;
  cpstr(mcfg.mqttHost, "emqx.aysafi.com", sizeof(mcfg.mqttHost));
  mcfg.mqttPort = 8883;
  mcfg.mqttTls  = true;
  mcfg.mqttUser[0] = '\0';
  mcfg.mqttPass[0] = '\0';
  cpstr(mcfg.mqttSite, "planta1", sizeof(mcfg.mqttSite));
  mcfg.mqttPubMs = 2000;

  mcfg.loraFreq  = 915.0f;
  mcfg.loraBw    = 125.0f;
  mcfg.loraSf    = 9;
  mcfg.loraCr    = 5;
  mcfg.loraSync  = 0x34;
  mcfg.loraPwr   = 14;

  mcfg.pollMs        = 250;
  mcfg.ackTimeoutMs  = 500;
  mcfg.offlineAfter  = 3;
  mcfg.pulseMs       = 500;

  mcfg.localIoEnabled = false;

  strncpy(mcfg.apSsid, "MasterIO-Setup", sizeof(mcfg.apSsid));
  strncpy(mcfg.apPass, "aysafi1234",     sizeof(mcfg.apPass));

  mcfg.nodeCount = 0;
  for (auto& n : mcfg.nodes) { n = MasterNode{}; }
}

void masterConfigLoad() {
  masterConfigFactory();
  Preferences p;
  if (!p.begin(NVS_NS, true)) return;
  bool blobOk = false;
  // 1) blob de features (solo si el layout coincide con este firmware)
  if (p.getUInt("magic", 0) == CFG_MAGIC &&
      p.getBytesLength("blob") == sizeof(MasterConfig)) {
    p.getBytes("blob", &mcfg, sizeof(MasterConfig));
    blobOk = true;
  }
  // 2) identidad: siempre (con lo anterior como default) -> gana
  bool idOk = p.getBool("id_set", false);
  identityLoad(p);
  p.end();

  // 3) red de seguridad: si la identidad de NVS quedo en blanco (primer arranque
  //    tras borrado/corrupcion), intentar restaurar la de campo desde LittleFS.
  if (!idOk && mcfg.nodeCount == 0 && idMirrorRestore()) {
    Preferences w;
    if (w.begin(NVS_NS, false)) { identitySave(w); w.end(); }
  }
  (void)blobOk;
}

bool masterConfigSave() {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;
  size_t n = p.putBytes("blob", &mcfg, sizeof(MasterConfig));
  p.putUInt("magic", CFG_MAGIC);
  identitySave(p);
  p.end();
  idMirrorWrite();
  return n == sizeof(MasterConfig);
}

bool masterConfigStored() {
  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;
  bool ok = (p.getUInt("magic", 0) == CFG_MAGIC) || p.getBool("id_set", false);
  p.end();
  return ok;
}

int masterFindByMac(const char* mac) {
  for (int i = 0; i < MASTER_MAX_NODES; i++)
    if (mcfg.nodes[i].addr && strcasecmp(mcfg.nodes[i].mac, mac) == 0) return i;
  return -1;
}

int masterFindByAddr(uint8_t addr) {
  for (int i = 0; i < MASTER_MAX_NODES; i++)
    if (mcfg.nodes[i].addr == addr) return i;
  return -1;
}

bool masterAddrFree(uint8_t addr) {
  if (addr < 1 || addr > 254) return false;
  if (addr == mcfg.masterLoraAddr) return false;
  return masterFindByAddr(addr) < 0;
}

int masterAddNode(uint8_t addr, const char* mac, const char* name) {
  int slot = masterFindByMac(mac);
  if (slot < 0) {
    for (int i = 0; i < MASTER_MAX_NODES; i++)
      if (mcfg.nodes[i].addr == 0) { slot = i; break; }
  }
  if (slot < 0) return -1;

  MasterNode& n = mcfg.nodes[slot];
  n.addr    = addr;
  strncpy(n.mac,  mac,  sizeof(n.mac) - 1);  n.mac[sizeof(n.mac) - 1]   = '\0';
  strncpy(n.name, name, sizeof(n.name) - 1); n.name[sizeof(n.name) - 1] = '\0';
  n.enabled = true;

  uint8_t cnt = 0;
  for (auto& x : mcfg.nodes) if (x.addr) cnt++;
  mcfg.nodeCount = cnt;
  return slot;
}

void masterRemoveNode(int slot) {
  if (slot < 0 || slot >= MASTER_MAX_NODES) return;
  mcfg.nodes[slot] = MasterNode{};
  uint8_t cnt = 0;
  for (auto& x : mcfg.nodes) if (x.addr) cnt++;
  mcfg.nodeCount = cnt;
}

uint32_t mbFormatToConfig(uint8_t f) {
  switch (f) {
    case 0: return SERIAL_8N1;
    case 1: return SERIAL_8E1;
    case 2: return SERIAL_8O1;
    case 3: return SERIAL_8N2;
    case 4: return SERIAL_8E2;
    case 5: return SERIAL_8O2;
    default: return SERIAL_8E1;
  }
}

String masterMac() {
  char b[13];
  snprintf(b, sizeof(b), "%012llX", (unsigned long long)ESP.getEfuseMac());
  return String(b);
}
