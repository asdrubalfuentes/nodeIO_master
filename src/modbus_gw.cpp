#include "modbus_gw.h"
#include "master_config.h"
#include "lora_master.h"
#include "io.h"
#include "log.h"
#include <ModbusRTU.h>

static ModbusRTU mb;

static const uint16_t NODE_BLK   = 16;
static const uint16_t GLOBAL_BASE = 900;   // Input Reg 900..915
static const uint16_t LOCAL_COIL  = 900;   // Coil 900..903

// ---- write callbacks ------------------------------------------------
static uint16_t onNodeCoil(TRegister* reg, uint16_t val) {
  uint16_t off  = reg->address.address;
  int      slot = off / NODE_BLK;
  uint16_t k    = off % NODE_BLK;
  if (slot < 0 || slot >= MASTER_MAX_NODES || !mcfg.nodes[slot].addr) return val;

  if (k < 4) {                       // relay setpoint -> absolute WR of all 4
    char want[4];
    for (uint16_t j = 0; j < 4; j++) {
      bool on = (j == k) ? (val != 0) : (mb.Coil(slot * NODE_BLK + j));
      want[j] = on ? '1' : '0';
    }
    masterQueueRelays(slot, want);
  } else if (k < 8) {                // pulse trigger
    if (val) masterQueuePulse(slot, (k - 4) + 1, mcfg.pulseMs);
    return 0;                        // self-clearing
  }
  return val;
}

static uint16_t onLocalCoil(TRegister* reg, uint16_t val) {
  if (!mcfg.localIoEnabled) return val;
  uint16_t k = reg->address.address - LOCAL_COIL;
  if (k < 4) ioSetRelay(k, val ? 1 : 0, 0x0F);
  return val;
}

// ---- setup --------------------------------------------------------
void modbusBegin() {
  uint32_t fmt = mbFormatToConfig(mcfg.mbFormat);

  if (mcfg.mbUsb) {
    // Modbus on the USB UART (UART0, GPIO43/44). Re-open the port that
    // heltec_setup() left at 115200 8N1 with the configured Modbus framing.
    Serial.end();
    Serial.begin(mcfg.mbBaud, fmt, mcfg.mbRxPin, mcfg.mbTxPin);
    mb.begin(&Serial);                 // direct link, no DE/RE
  } else {
    Serial1.begin(mcfg.mbBaud, fmt, mcfg.mbRxPin, mcfg.mbTxPin);
    if (mcfg.mbDePin >= 0) mb.begin(&Serial1, mcfg.mbDePin);
    else                   mb.begin(&Serial1);
  }
  mb.setBaudrate(mcfg.mbBaud);
  mb.slave(mcfg.mbSlaveId);

  for (int i = 0; i < MASTER_MAX_NODES; i++) {
    uint16_t b = i * NODE_BLK;
    mb.addIreg(b, 0, NODE_BLK);
    mb.addIsts(b, false, 8);
    mb.addCoil(b, false, 8);
    mb.onSetCoil(b, onNodeCoil, 8);
  }
  mb.addIreg(GLOBAL_BASE, 0, 16);

  if (mcfg.localIoEnabled) {
    mb.addCoil(LOCAL_COIL, false, 4);
    mb.onSetCoil(LOCAL_COIL, onLocalCoil, 4);
  }

  LOGF("[modbus] RTU slave %u @ %lu fmt %u  %s\n",
       mcfg.mbSlaveId, (unsigned long)mcfg.mbBaud, mcfg.mbFormat,
       mcfg.mbUsb ? "USB UART0" : "Serial1 RS-485");
}

// ---- publish ----------------------------------------------------
static void publish() {
  uint8_t online = 0;
  for (int i = 0; i < MASTER_MAX_NODES; i++) {
    uint16_t b = i * NODE_BLK;
    MasterNode&   nd = mcfg.nodes[i];
    NodeSnapshot& s  = snap[i];

    mb.Ireg(b + 9, nd.addr);
    if (!nd.addr) {
      mb.Ireg(b + 6, 0);
      mb.Ists(b + 4, false);
      continue;
    }
    for (uint16_t k = 0; k < 4; k++) mb.Ireg(b + k, s.ai[k]);

    uint16_t di = 0, ro = 0;
    for (uint16_t k = 0; k < 4; k++) {
      if (s.di[k])        di |= (1 << k);
      if (s.ro[k] == '1') ro |= (1 << k);
      if (s.ro[k] == 'x') ro |= (1 << (8 + k));
      mb.Ists(b + k, s.di[k]);
    }
    mb.Ireg(b + 4, di);
    mb.Ireg(b + 5, ro);
    mb.Ireg(b + 6, s.online ? 1 : 0);
    mb.Ireg(b + 7, (uint16_t)s.rssi);

    uint32_t ago = s.lastReplyMs ? (millis() - s.lastReplyMs) / 1000UL : 65535UL;
    mb.Ireg(b + 8, (uint16_t)(ago > 65535UL ? 65535UL : ago));
    mb.Ists(b + 4, s.online);

    if (nd.enabled && s.online) online++;
  }

  mb.Ireg(GLOBAL_BASE + 0, 0x0203);            // proto marker
  mb.Ireg(GLOBAL_BASE + 1, mcfg.nodeCount);
  mb.Ireg(GLOBAL_BASE + 2, online);
  mb.Ireg(GLOBAL_BASE + 3, mcfg.localIoEnabled ? 1 : 0);

  if (mcfg.localIoEnabled) {
    for (uint16_t k = 0; k < 4; k++) mb.Ireg(GLOBAL_BASE + 4 + k, ioReadAnalog(k));
    uint16_t ldi = 0, lro = 0;
    for (uint16_t k = 0; k < 4; k++) {
      if (ioReadDigital(k)) ldi |= (1 << k);
      if (ioGetRelay(k))    lro |= (1 << k);
    }
    mb.Ireg(GLOBAL_BASE + 8, ldi);
    mb.Ireg(GLOBAL_BASE + 9, lro);
  }
}

void modbusTask() {
  mb.task();
  static uint32_t last = 0;
  if (millis() - last >= 100) { last = millis(); publish(); }
}
