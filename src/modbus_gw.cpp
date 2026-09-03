#include "modbus_gw.h"
#include "master_config.h"
#include "lora_master.h"
#include "net_master.h"
#include "io.h"
#include "log.h"
#include <ModbusRTU.h>
#include <ModbusIP_ESP8266.h>

// Dos backends con la MISMA API (ModbusAPI<T>): el mapa y la publicacion se
// escriben una vez con plantillas y se aplican al/los transporte(s) activo(s).
// MAPA A del contrato ORCHESTRATION/REGISTER_MAP.md  (CONTRACT_VERSION 1).
static ModbusRTU mbRtu;
static ModbusIP  mbTcp;

static bool rtuOn = false, tcpOn = false, tcpStarted = false;

static const uint16_t NODE_BLK   = 16;
static const uint16_t GLOBAL_BASE = 900;   // Input Reg 900..915
static const uint16_t LOCAL_COIL  = 900;   // Coil 900..903

// Sombra de la consigna de reles por nodo. Evita depender de "que instancia"
// recibio la escritura cuando hay dos backends activos.
static bool relaySet[MASTER_MAX_NODES][4] = {};

// ---- write callbacks ----------------------------------------------------
static uint16_t onNodeCoil(TRegister* reg, uint16_t val) {
  uint16_t off  = reg->address.address;
  int      slot = off / NODE_BLK;
  uint16_t k    = off % NODE_BLK;
  if (slot < 0 || slot >= MASTER_MAX_NODES || !mcfg.nodes[slot].addr) return val;

  if (k < 4) {                       // consigna de rele -> WR absoluto de los 4
    relaySet[slot][k] = (val != 0);
    char want[4];
    for (uint16_t j = 0; j < 4; j++) want[j] = relaySet[slot][j] ? '1' : '0';
    masterQueueRelays(slot, want);
  } else if (k < 8) {                // disparo de pulso
    if (val) masterQueuePulse(slot, (k - 4) + 1, mcfg.pulseMs);
    return val;                      // publish() lo limpia en el siguiente ciclo
  }
  return val;
}

static uint16_t onLocalCoil(TRegister* reg, uint16_t val) {
  if (!mcfg.localIoEnabled) return val;
  uint16_t k = reg->address.address - LOCAL_COIL;
  if (k < 4) ioSetRelay(k, val ? 1 : 0, 0x0F);
  return val;
}

// ---- registro del mapa (identico en RTU y TCP) ------------------------
template <class MB>
static void mapRegister(MB& mb) {
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
}

// ---- setup ------------------------------------------------------------
void modbusBegin() {
  rtuOn = (mcfg.mbTransport == MBT_RTU  || mcfg.mbTransport == MBT_BOTH);
  tcpOn = (mcfg.mbTransport == MBT_TCP  || mcfg.mbTransport == MBT_BOTH);
  tcpStarted = false;

  if (rtuOn) {
    uint32_t fmt = mbFormatToConfig(mcfg.mbFormat);
    if (mcfg.mbUsb) {
      Serial.end();
      Serial.begin(mcfg.mbBaud, fmt, mcfg.mbRxPin, mcfg.mbTxPin);
      mbRtu.begin(&Serial);                 // enlace directo, sin DE/RE
    } else {
      Serial1.begin(mcfg.mbBaud, fmt, mcfg.mbRxPin, mcfg.mbTxPin);
      if (mcfg.mbDePin >= 0) mbRtu.begin(&Serial1, mcfg.mbDePin);
      else                   mbRtu.begin(&Serial1);
    }
    mbRtu.setBaudrate(mcfg.mbBaud);
    mbRtu.slave(mcfg.mbSlaveId);
    mapRegister(mbRtu);
    LOGF("[modbus] RTU slave %u @ %lu fmt %u  %s\n",
         mcfg.mbSlaveId, (unsigned long)mcfg.mbBaud, mcfg.mbFormat,
         mcfg.mbUsb ? "USB UART0" : "Serial1 RS-485");
  }

  if (tcpOn)
    LOGF("[modbus] TCP servidor :%u (arranca al asociar WiFi STA)\n",
         mcfg.mbTcpPort ? mcfg.mbTcpPort : 502);
}

// ---- publish (identico en RTU y TCP) --------------------------------
template <class MB>
static void publishTo(MB& mb) {
  uint8_t online = 0;
  for (int i = 0; i < MASTER_MAX_NODES; i++) {
    uint16_t b = i * NODE_BLK;
    MasterNode&   nd = mcfg.nodes[i];
    NodeSnapshot& s  = snap[i];

    for (uint16_t k = 4; k < 8; k++)         // auto-limpia coils de disparo de pulso
      if (mb.Coil(b + k)) mb.Coil(b + k, false);

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

  mb.Ireg(GLOBAL_BASE + 0, 0x0203);            // proto marker (contrato Seccion 3.2)
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
  // Arranca el servidor TCP cuando la WiFi STA ya tiene IP (evita bind sin red).
  if (tcpOn && !tcpStarted && netStaUp()) {
    mbTcp.server(mcfg.mbTcpPort ? mcfg.mbTcpPort : 502);
    mapRegister(mbTcp);
    tcpStarted = true;
    LOGF("[modbus] TCP servidor escuchando en %s:%u\n",
         netStaIp().c_str(), mcfg.mbTcpPort ? mcfg.mbTcpPort : 502);
  }

  if (rtuOn)      mbRtu.task();
  if (tcpStarted) mbTcp.task();

  static uint32_t last = 0;
  if (millis() - last >= 100) {
    last = millis();
    if (rtuOn)      publishTo(mbRtu);
    if (tcpStarted) publishTo(mbTcp);
  }
}

bool modbusTcpReady() { return tcpStarted; }
