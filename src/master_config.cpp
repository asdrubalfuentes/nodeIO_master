#include "master_config.h"
#include <Preferences.h>

MasterConfig mcfg;

static const char*    NVS_NS    = "masterio";
static const uint32_t CFG_MAGIC = 0xA75A3E01;   // bump on struct layout change

void masterConfigFactory() {
  mcfg = MasterConfig{};
  mcfg.masterLoraAddr = 200;

  mcfg.mbSlaveId = 1;
  mcfg.mbBaud    = 19200;
  mcfg.mbFormat  = 1;              // 8E1
  mcfg.mbRxPin   = 2;
  mcfg.mbTxPin   = 3;
  mcfg.mbDePin   = 4;

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
  if (p.getUInt("magic", 0) == CFG_MAGIC &&
      p.getBytesLength("blob") == sizeof(MasterConfig)) {
    p.getBytes("blob", &mcfg, sizeof(MasterConfig));
  }
  p.end();
}

bool masterConfigSave() {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;
  size_t n = p.putBytes("blob", &mcfg, sizeof(MasterConfig));
  p.putUInt("magic", CFG_MAGIC);
  p.end();
  return n == sizeof(MasterConfig);
}

bool masterConfigStored() {
  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;
  bool ok = (p.getUInt("magic", 0) == CFG_MAGIC);
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
