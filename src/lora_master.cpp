#include "lora_master.h"
#include "log.h"
#include <RadioLib.h>
#include <CRC32.h>

extern SX1262 radio;   // defined in heltec_unofficial.h (main.cpp only)

NodeSnapshot  snap[MASTER_MAX_NODES]      = {};
DiscoveredNode discovered[MASTER_MAX_NODES] = {};
uint8_t        discoveredCount             = 0;

static const uint8_t BCAST = 255;

static volatile bool rxFlag = false;
static uint8_t  seq = 0;

struct Pending { bool wr; char want[4]; bool wp; uint8_t wpIdx; uint16_t wpMs; };
static Pending pending[MASTER_MAX_NODES] = {};

// poll state machine
static int      curSlot      = -1;
static uint32_t lastActionMs = 0;
static bool     waiting      = false;
static uint32_t waitSince    = 0;
static int      waitSlot     = -1;

static void IRAM_ATTR onDio1() { rxFlag = true; }

// ---- frame TX/RX -----------------------------------------------------
static void sendFrame(uint8_t dst, const char* body) {
  char text[220];
  int n = snprintf(text, sizeof(text), "%u,%u,%u,%s",
                   dst, mcfg.masterLoraAddr, seq++, body);
  if (n <= 0 || n >= (int)sizeof(text)) return;

  uint32_t crc = CRC32::calculate((const uint8_t*)text, n);
  uint8_t frame[255];
  memcpy(frame, text, n);
  memcpy(frame + n, &crc, 4);
  frame[n + 4] = '\n';
  radio.transmit(frame, n + 5);
  radio.startReceive();
}

// Reads a pending packet, verifies CRC. Returns text length (NUL-terminated in
// out) or 0 if nothing valid. Clears rxFlag.
static int readFrameNow(char* out, size_t outSz) {
  if (!rxFlag) return 0;
  rxFlag = false;

  uint8_t buf[255];
  int len = radio.getPacketLength();
  int16_t st = radio.readData(buf, len);
  radio.startReceive();
  if (st != RADIOLIB_ERR_NONE || len < 5) return 0;
  if (buf[len - 1] == '\n') len--;
  if (len < 5) return 0;

  size_t textLen = len - 4;
  uint32_t rxCrc;
  memcpy(&rxCrc, buf + textLen, 4);
  if (CRC32::calculate(buf, textLen) != rxCrc) return 0;
  if (textLen >= outSz) return 0;

  memcpy(out, buf, textLen);
  out[textLen] = '\0';
  return (int)textLen;
}

// ---- reply parsing (poll context) ----------------------------------
static void parseStatus(int slot, char*& save) {
  NodeSnapshot& s = snap[slot];
  for (uint8_t i = 0; i < 4; i++) { char* a = strtok_r(nullptr, ",", &save); s.ai[i] = a ? (uint16_t)atoi(a) : 0; }
  for (uint8_t i = 0; i < 4; i++) { char* d = strtok_r(nullptr, ",", &save); s.di[i] = d ? (uint8_t)atoi(d) : 0; }
  for (uint8_t i = 0; i < 4; i++) { char* o = strtok_r(nullptr, ",", &save); s.ro[i] = o ? o[0] : 'x'; }
}

static void dispatchReply(char* text) {
  char* save = nullptr;
  char* t_dst  = strtok_r(text, ",", &save);
  char* t_src  = strtok_r(nullptr, ",", &save);
  strtok_r(nullptr, ",", &save);                  // seq (unused)
  char* t_resp = strtok_r(nullptr, ",", &save);
  if (!t_dst || !t_src || !t_resp) return;
  if ((uint8_t)atoi(t_dst) != mcfg.masterLoraAddr) return;

  int slot = masterFindByAddr((uint8_t)atoi(t_src));
  if (slot < 0) return;

  NodeSnapshot& s = snap[slot];
  s.rssi        = (int16_t)radio.getRSSI();
  s.lastReplyMs = millis();
  s.online      = true;
  s.misses      = 0;
  if (waiting && slot == waitSlot) waiting = false;

  if (!strcmp(t_resp, "ST")) parseStatus(slot, save);
}

// ---- slot rotation -------------------------------------------------
static int nextEnabledSlot(int from) {
  for (int k = 1; k <= MASTER_MAX_NODES; k++) {
    int i = (from + k) % MASTER_MAX_NODES;
    if (mcfg.nodes[i].addr && mcfg.nodes[i].enabled) return i;
  }
  return -1;
}

// ---- public API --------------------------------------------------
bool masterBegin() {
  int16_t st = radio.begin(mcfg.loraFreq, mcfg.loraBw, mcfg.loraSf,
                           mcfg.loraCr, mcfg.loraSync, mcfg.loraPwr, 8);
  if (st != RADIOLIB_ERR_NONE) {
    LOGF("[lora] begin() fallo, code %d\n", st);
    return false;
  }
  radio.setDio1Action(onDio1);
  radio.startReceive();
  LOGF("[lora] GATEWAY %.1f MHz SF%u BW%.0f addr %u, %u nodo(s)\n",
       mcfg.loraFreq, mcfg.loraSf, mcfg.loraBw, mcfg.masterLoraAddr, mcfg.nodeCount);
  return true;
}

void masterStandby() { radio.standby(); }

void masterQueueRelays(int slot, const char want[4]) {
  if (slot < 0 || slot >= MASTER_MAX_NODES) return;
  memcpy(pending[slot].want, want, 4);
  pending[slot].wr = true;
}

void masterQueuePulse(int slot, uint8_t idx1, uint16_t ms) {
  if (slot < 0 || slot >= MASTER_MAX_NODES) return;
  pending[slot].wpIdx = idx1;
  pending[slot].wpMs  = ms;
  pending[slot].wp    = true;
}

void masterPollLoop() {
  char text[220];
  if (rxFlag) { int L = readFrameNow(text, sizeof(text)); if (L > 0) dispatchReply(text); }

  uint32_t now = millis();

  if (waiting && now - waitSince > mcfg.ackTimeoutMs) {
    waiting = false;
    if (waitSlot >= 0 && ++snap[waitSlot].misses >= mcfg.offlineAfter)
      snap[waitSlot].online = false;
  }

  if (!waiting && now - lastActionMs >= mcfg.pollMs) {
    int next = nextEnabledSlot(curSlot);
    if (next < 0) return;
    curSlot      = next;
    lastActionMs = now;
    uint8_t addr = mcfg.nodes[curSlot].addr;
    Pending& q   = pending[curSlot];

    if (q.wr) {
      char b[24];
      snprintf(b, sizeof(b), "WR,%c,%c,%c,%c", q.want[0], q.want[1], q.want[2], q.want[3]);
      sendFrame(addr, b);
      q.wr = false;
    } else if (q.wp) {
      char b[24];
      snprintf(b, sizeof(b), "WP,%u,%u", q.wpIdx, q.wpMs);
      sendFrame(addr, b);
      q.wp = false;
    } else {
      sendFrame(addr, "RD");
    }
    waiting = true; waitSince = now; waitSlot = curSlot;
  }
}

// ---- discovery / adoption (blocking, portal only) ----------------
uint8_t masterDiscover(uint16_t windowMs) {
  discoveredCount = 0;
  for (int attempt = 0; attempt < 3 && discoveredCount == 0; attempt++) {
    sendFrame(BCAST, "DISC");
    uint32_t t0 = millis();
    while (millis() - t0 < windowMs) {
      char text[220];
      int L = readFrameNow(text, sizeof(text));
      if (L <= 0) { delay(2); continue; }

      char* save = nullptr;
      strtok_r(text, ",", &save);                 // dst
      strtok_r(nullptr, ",", &save);              // src (0 = unadopted)
      strtok_r(nullptr, ",", &save);              // seq
      char* resp = strtok_r(nullptr, ",", &save);
      if (!resp || strcmp(resp, "IAM")) continue;
      char* mac = strtok_r(nullptr, ",", &save);
      char* fw  = strtok_r(nullptr, ",", &save);
      if (!mac) continue;
      if (masterFindByMac(mac) >= 0) continue;    // already adopted by us

      bool dup = false;
      for (uint8_t i = 0; i < discoveredCount; i++)
        if (!strcasecmp(discovered[i].mac, mac)) { dup = true; break; }
      if (dup || discoveredCount >= MASTER_MAX_NODES) continue;

      strncpy(discovered[discoveredCount].mac, mac, 12); discovered[discoveredCount].mac[12] = '\0';
      strncpy(discovered[discoveredCount].fw, fw ? fw : "?", 11); discovered[discoveredCount].fw[11] = '\0';
      discoveredCount++;
    }
  }
  return discoveredCount;
}

bool masterAdopt(const char* mac, uint8_t addr, const char* name) {
  if (!masterAddrFree(addr)) return false;

  char body[96];
  snprintf(body, sizeof(body), "ADOPT,%s,%u,%.1f,%u,%.1f,%u,0x%02X,%d",
           mac, addr, mcfg.loraFreq, mcfg.loraSf, mcfg.loraBw,
           mcfg.loraCr, mcfg.loraSync, mcfg.loraPwr);

  for (int attempt = 0; attempt < 3; attempt++) {
    sendFrame(BCAST, body);
    uint32_t t0 = millis();
    while (millis() - t0 < 1500) {
      char text[220];
      int L = readFrameNow(text, sizeof(text));
      if (L <= 0) { delay(2); continue; }
      char* save = nullptr;
      strtok_r(text, ",", &save);                 // dst
      strtok_r(nullptr, ",", &save);              // src
      strtok_r(nullptr, ",", &save);              // seq
      char* resp = strtok_r(nullptr, ",", &save);
      char* amac = strtok_r(nullptr, ",", &save);
      if (resp && !strcmp(resp, "ACK") && amac && !strcasecmp(amac, mac)) {
        int slot = masterAddNode(addr, mac, name && name[0] ? name : mac);
        if (slot < 0) return false;
        snap[slot] = NodeSnapshot{};
        masterConfigSave();
        return true;
      }
    }
  }
  return false;
}

bool masterRelease(int slot) {
  if (slot < 0 || slot >= MASTER_MAX_NODES || !mcfg.nodes[slot].addr) return false;
  char body[32];
  snprintf(body, sizeof(body), "RELEASE,%s", mcfg.nodes[slot].mac);
  for (int i = 0; i < 3; i++) { sendFrame(mcfg.nodes[slot].addr, body); delay(150); }
  masterRemoveNode(slot);
  masterConfigSave();
  return true;
}
