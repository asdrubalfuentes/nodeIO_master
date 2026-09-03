# nodeIO_master — README del programador

Pasarela **LoRa ↔ Modbus** para **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262).
Sondea hasta 8 nodos `nodeIO` por LoRa y los expone como **servidor Modbus**:
**TCP :502 sobre WiFi STA** (por defecto — es la vía hacia el PLC LOGO! 9 / PLC-SIM)
y/o **RTU** en RS-485/USB (respaldo de banco). El mapa es el **MAPA A** del
contrato [`../ORCHESTRATION/REGISTER_MAP.md`](../ORCHESTRATION/REGISTER_MAP.md).
El descubrimiento y la adopción de nodos se hacen desde su portal cautivo.

Antes fue un clon de `../nodeIO`; hoy solo comparte `src/io.{h,cpp}` y
`src/images.h` byte a byte.

---

## 1. Toolchain

```sh
pio run
pio run -t upload
pio device monitor -b 115200      # solo log; Modbus va por RS-485
```

`platformio.ini` → `lib_deps`: `ropg/Heltec_ESP32_LoRa_v3`, `bakercp/CRC32`,
`emelianov/modbus-esp8266`.

---

## 2. Arquitectura

| Módulo | Responsabilidad |
|---|---|
| `src/main.cpp` | Estados `MODE_NORMAL` / `MODE_PORTAL`, splash, OLED de estado, long-press de BUTTON_1. **Único TU que incluye `heltec_unofficial.h`.** |
| `src/master_config.{h,cpp}` | `MasterConfig` en NVS (namespace `masterio`, blob + magic): LoRa, transporte Modbus (`mbTransport`, `mbTcpPort`), RTU, **WiFi STA** (`staSsid/staPass/staStatic/staIp/...`), polling, IO local, AP y **tabla de nodos** `MasterNode nodes[8]`. Helpers `masterFindByMac/Addr`, `masterAddNode`, `masterRemoveNode`, `mbFormatToConfig`. |
| `src/lora_master.{h,cpp}` | Lado LoRa: `masterBegin()`, `masterPollLoop()` (round-robin `RD`, cola de escritura `WR`/`WP`, marcado online/offline en `snap[8]`), y los bloqueantes `masterDiscover()` / `masterAdopt()` / `masterRelease()` usados por el portal. |
| `src/net_master.{h,cpp}` | WiFi STA para Modbus TCP: `netBegin()` (arranca STA si `mcfg.staEnabled`), `netLoop()` (FSM de reconexión), `netStaUp()` / `netStaIp()`. Independiente de la radio LoRa. Solo en MODO NORMAL; el portal sigue en SoftAP. |
| `src/modbus_gw.{h,cpp}` | **`ModbusRTU` + `ModbusIP`** (misma API `ModbusAPI<T>`). El mapa y `publish()` se escriben una vez con plantillas (`mapRegister<>`, `publishTo<>`) y se aplican al/los transporte(s) activo(s) según `mcfg.mbTransport`. El servidor TCP arranca cuando la WiFi STA obtiene IP. Callbacks `onSetCoil` → `masterQueueRelays/Pulse` (con sombra `relaySet[][]` para no depender de qué backend recibió la escritura). |
| `src/portal_master.{h,cpp}` | Portal cautivo (DNS :53 `*`, WebServer :80). Rutas `/` `/save` `/scan` `/adopt` `/remove` `/toggle`. La radio sigue viva en modo portal para descubrir/adoptar. |
| `src/io.{h,cpp}` | Copia de `nodeIO` (pines/ISR/relés). Aquí solo se usan los botones y, si `localIoEnabled`, las lecturas AI/DI. |

`lora_master.cpp` y `modbus_gw.cpp` incluyen `<RadioLib.h>` / `<ModbusRTU.h>` y
`extern SX1262 radio;` (la instancia global vive en `heltec_unofficial.h`,
incluido solo por `main.cpp`).

Dependencias entre módulos: `modbus_gw` → `lora_master` (lee `snap[]`, encola
escrituras). `portal_master` → `lora_master` + `master_config`. Sin ciclos.

---

## 3. Flujo

```
setup(): heltec_setup(); masterConfigLoad(); ioInit(0,0); splash();
         nodeCount == 0 ? { masterBegin(); enterPortal(); }
                        : { masterBegin(); netBegin(); modbusBegin(); MODE_NORMAL }

loop():  heltec_loop();
         BUTTON_1 > 3 s -> enterPortal()   (para polling+Modbus; radio sigue viva)
         MODE_PORTAL: portalLoop(); drawPortalScreen(); return;
         MODE_NORMAL: netLoop(); masterPollLoop(); modbusTask(); drawStatusScreen();
```

`modbusTask()` arranca el servidor Modbus TCP la primera vez que `netStaUp()` es
cierto (evita hacer `bind` sin red), luego sirve `task()` y publica `snap[]`.

`masterPollLoop()` es una máquina de estados no bloqueante: cada `pollMs` avanza
al siguiente slot habilitado y envía `RD` (o el `WR`/`WP` encolado); espera
`ackTimeoutMs`; `offlineAfter` fallos → `snap[i].online = false`.

---

## 4. Protocolo LoRa

Misma trama que el nodo (ver `../nodeIO/PROTOCOL.md`), incluidos los añadidos de
aprovisionamiento:

- `DISC` (→255) → el gateway recoge `IAM,<mac>,<fw>` de los nodos sin adoptar.
- `ROLLCALL` (→255) → los nodos **ya adoptados** responden `HERE,<mac>,<addr>,<masterAddr>`.
  `masterRollcall()` reconstruye `mcfg.nodes[]` desde ahí (solo los que dicen ser
  suyos y con dirección libre) y persiste. Se lanza al arrancar con la tabla vacía
  y desde el botón del portal. Los nodos **no** se des-adoptan; `RELEASE` sigue
  siendo la única vía. `dispatchReply()` también absorbe balizas `HERE` no
  solicitadas durante el sondeo normal.
- `ADOPT,<mac>,<addr>,<freq>,<sf>,<bw>,<cr>,<sync>,<pwr>` (→255) → el nodo guarda
  dirección + canal y responde `ACK,<mac>`.
- `RELEASE,<mac>` → el nodo vuelve a "sin adoptar".
- Operación: `RD` → `ST,...`; `WR,<r1..r4>`; `WP,<idx>,<ms>`.

El gateway usa `mcfg.masterLoraAddr` (def. 200) como `src` y `mcfg.lora*` como
canal autoritativo, que empuja en cada `ADOPT`.

---

## 5. Mapa Modbus (MAPA A del contrato)

Es el **MAPA A** de [`../ORCHESTRATION/REGISTER_MAP.md`](../ORCHESTRATION/REGISTER_MAP.md),
**congelado byte a byte**. Definido en `modbus_gw.h`. Unit ID `mcfg.mbSlaveId`
(el servidor TCP responde con cualquier Unit ID). Bloque por nodo `i` = 0..7,
base `i*16`: Input Reg (AI/DI/relés/link/RSSI/edad/addr/fw), Discrete Inputs (DI +
link), Coils (consigna de relé + disparo de pulso). Bloque global en Ireg 900;
bloque IO local (si `localIoEnabled`) en Ireg 904+ y Coil 900+.

**Transporte** (`mcfg.mbTransport`): `MBT_TCP` (def., `:mcfg.mbTcpPort` = 502
sobre WiFi STA), `MBT_RTU` (RS-485/USB), `MBT_BOTH`. En `MBT_BOTH` una escritura
de coil en un backend no se refleja en el otro (bench; en planta se usa solo TCP).

> **Actualización de firmware:** este cambio sube `CFG_MAGIC` (03). Al arrancar
> con la nueva versión, la NVS vieja no valida y la tabla de nodos queda vacía —
> pero el arranque lanza **`ROLLCALL`** y la reconstruye sola desde los nodos en
> el campo (que ya **no** se des-adoptan). Si algún nodo no aparece, usa el botón
> ROLLCALL del portal o re-adóptalo. Configura la WiFi STA en el portal antes de
> dejarlo en modo normal.

---

## 6. Cómo extender

**Nuevo campo de config:** añádelo a `MasterConfig`, default en
`masterConfigFactory()`, **sube `CFG_MAGIC`** en `master_config.cpp`, e input en
`portal_master.cpp::buildPage()` + parseo en `handleSave()`.

**Nuevo registro Modbus:** amplía `NODE_BLK` o el bloque global en `modbus_gw`,
dando de alta en `modbusBegin()` y publicando en `publish()`.

**Otro comando LoRa saliente:** añade un `sendFrame(addr, "XX,...")` y, si espera
respuesta, un caso en `dispatchReply()`.

**Multi-master:** cada nodo solo obedece `ADOPT`/`RELEASE` con su MAC y, ya
adoptado, filtra por `masterAddr`. Para mover un nodo, `masterRelease()` en un
gateway y `masterAdopt()` en el otro.

---

## 7. Sincronización con nodeIO

Solo `src/io.{h,cpp}` y `src/images.h` deben mantenerse idénticos entre ambos
proyectos. `platformio.ini` diverge (este añade `modbus-esp8266`). Todo lo demás
es específico del gateway.

---

## 8. Notas de hardware

- UART1 Modbus por defecto en GPIO2/3 + DE GPIO4 = regleta AI del carrier; válido
  con `localIoEnabled = false`. Configurable en el portal.
- `mb.begin(&Serial1, dePin)` gestiona el DE/RE del RS-485; `dePin = -1` → UART TTL
  sin control de dirección.
- WiFi (2.4 GHz, radio interna del ESP32-S3) y LoRa (915 MHz, SX1262 por SPI) son
  independientes: coexisten sin interferir. `net_master.cpp` usa `WiFi.setSleep(false)`
  para latencia de Modbus TCP (el gateway va con alimentación fija).
- **STA vs portal:** en MODO NORMAL, si `staEnabled`, el gateway se une a la WiFi
  de planta (STA). El portal cautivo sigue siendo SoftAP y solo vive en MODO
  CONFIG; al guardar se reinicia y vuelve a STA.
