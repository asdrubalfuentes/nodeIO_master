# Manual de uso — Master IO (pasarela LoRa ↔ Modbus RTU)

Placa: **Heltec WiFi LoRa 32 V3**. Firmware: `Master IO gateway`.

El Master IO es una **pasarela**: por un lado sondea por radio LoRa hasta **8
nodos `nodeIO`** remotos; por el otro es un **esclavo Modbus RTU** en el puerto
serie (RS-485). Un PLC / SCADA lee por Modbus las entradas analógicas y digitales
de cada nodo y escribe sus salidas de relé. El Master **no usa sus propias
entradas/relés** salvo que se active "IO local" en el portal.

---

## 1. Conexiones

| Señal | Pin (por defecto, configurable) | Notas |
|---|---|---|
| Modbus RTU RX | GPIO2 | a RO del transceptor RS-485 |
| Modbus RTU TX | GPIO3 | a DI del transceptor RS-485 |
| Modbus RTU DE/RE | GPIO4 | control de dirección del RS-485 (−1 = sin control, TTL) |
| Botón BUTTON_1 | GPIO47 | pulsación larga (~3 s) → portal de configuración |
| USB-C | — | **solo consola de log** (115200); el bus Modbus va por RS-485 |

> Los pines de Modbus por defecto (GPIO2/3/4) son la regleta de entradas
> analógicas del carrier. Solo es válido con "IO local" **desactivado** (que es lo
> normal). Si activas IO local, cambia esos pines en el portal.

---

## 2. Puesta en marcha

1. Flashea uno o varios `nodeIO`. Al no estar adoptados muestran **"SIN ADOPTAR"**
   y su **MAC** en la OLED, y quedan a la escucha.
2. Flashea el Master IO. Con la tabla de nodos vacía **arranca directamente en el
   portal**.
3. Conéctate por WiFi a `MasterIO-Setup` / `aysafi1234` y abre `http://192.168.4.1`.
4. **Descubrir nodos** → "Buscar nodos sin adoptar". Aparecen las MAC encontradas.
5. Para cada una: teclea una **dirección LoRa** (1–254, única) y un nombre →
   "Agregar". El nodo responde, se guarda en la tabla y **reinicia adoptado**.
6. Repite para los demás nodos (hasta 8).
7. Ajusta **Modbus RTU** (Slave ID, baud, formato, pines) según tu cableado y pulsa
   **Guardar y reiniciar**.
8. El Master arranca en modo normal (OLED: `Nodos online 2/2`, `Modbus 1 @ 19200 485`).

---

## 3. Operación normal

La pantalla OLED muestra:

```
MASTER IO / gateway
Nodos online 2/2          <- nodos que responden / configurados
Modbus 1 @ 19200 485      <- slave id, baudios, RS-485 (o ttl)
ult. addr 1 rssi -78      <- último nodo que contestó y su señal
```

El Master recorre en bucle su tabla de nodos, envía `RD` a cada uno y publica la
respuesta en los registros Modbus. Un nodo que falla `Fallos->OFFLINE` veces
seguidas se marca offline.

Pulsación larga de **BUTTON_1** → vuelve al portal (se detiene el polling y
Modbus; la radio sigue disponible para descubrir/adoptar).

---

## 4. Mapa Modbus

Esclavo con el **Slave ID** configurado (por defecto 1). Bloque por nodo `i`
(0–7), base `i*16`:

| Función Modbus | Offset | Contenido |
|---|---|---|
| Input Register (FC04) | `i*16 + 0..3` | AI1..AI4 del nodo (0–4095) |
| | `i*16 + 4` | bits de entradas digitales (bit0..3 = DI1..DI4) |
| | `i*16 + 5` | bits de relés (bit0..3 = estado, bit8..11 = deshabilitado `x`) |
| | `i*16 + 6` | enlace: 0 = offline, 1 = online |
| | `i*16 + 7` | RSSI en dBm (int16 con signo) |
| | `i*16 + 8` | segundos desde la última respuesta |
| | `i*16 + 9` | dirección LoRa del nodo (0 = ranura vacía) |
| Discrete Input (FC02) | `i*16 + 0..3` | DI1..DI4 |
| | `i*16 + 4` | enlace online |
| Coil (FC01/05/15) | `i*16 + 0..3` | consigna de relé RO1..RO4 (escribir 1 = cerrar) |
| | `i*16 + 4..7` | disparo de pulso RO1..RO4 (escribir 1 → pulso; se autolimpia) |

Bloque global (Input Register):

| Offset | Contenido |
|---|---|
| 900 | marca de versión (0x0203) |
| 901 | nº de nodos configurados |
| 902 | nº de nodos online |
| 903 | IO local activada (0/1) |
| 904–907 | (si IO local) AI1..AI4 del propio Master |
| 908 | (si IO local) bits DI del Master |
| 909 | (si IO local) bits relés del Master |

Coils 900–903: (si IO local) relés del propio Master.

Ejemplo con `mbpoll`:

```
mbpoll -m rtu -b 19200 -P even -a 1 -t 3 -r 1 -c 16 /dev/ttyUSB0   # Ireg nodo 0
mbpoll -m rtu -b 19200 -P even -a 1 -t 0 -r 1 /dev/ttyUSB0 1       # cerrar relé 1 del nodo 0
```

---

## 5. Portal de configuración

Se entra con **BUTTON_1 largo (~3 s)** o automáticamente si no hay nodos.
Secciones:

- **Nodos adoptados**: tabla con dirección, nombre, MAC y estado. Botones
  *Activar/Desactivar* y *Quitar* (envía `RELEASE` y libera la ranura).
- **Descubrir nodos**: "Buscar" hace un barrido LoRa (`DISC`); lista las MAC sin
  adoptar; por cada una, dirección (obligatoria) + nombre → "Agregar".
- **LoRa**: dirección de la pasarela, frecuencia, BW, SF, CR, sync, potencia, y
  tiempos de sondeo / timeout / fallos-para-offline / ancho de pulso. **Este es el
  canal que se empuja a los nodos al adoptarlos.**
- **Modbus RTU**: Slave ID, baudios, formato (8N1…8O2), pines RX/TX/DE.
- **IO local**: casilla para leer las entradas/relés del propio carrier
  (por defecto desactivada).
- **WiFi del portal**: SSID y clave.

"Guardar y reiniciar" persiste todo en memoria no volátil.

---

## 6. Valores de fábrica

| Parámetro | Valor |
|---|---|
| Dirección LoRa de la pasarela | 200 |
| LoRa | 915.0 MHz · BW 125 kHz · SF 9 · CR 4/5 · sync 0x34 · 14 dBm |
| Sondeo / timeout / offline | 250 ms / 500 ms / 3 fallos |
| Ancho de pulso (coil) | 500 ms |
| Modbus | Slave 1 · 19200 · 8E1 · RX GPIO2 · TX GPIO3 · DE GPIO4 |
| IO local | desactivada |
| WiFi portal | `MasterIO-Setup` / `aysafi1234` |
| Nodos | ninguno (arranca en el portal) |

---

## 7. Problemas frecuentes

| Síntoma | Causa probable / solución |
|---|---|
| "Buscar" no encuentra el nodo | el nodo ya está adoptado (por este u otro master); o canal LoRa distinto del de fábrica; acerca las antenas |
| Adopción falla ("sin ACK") | la dirección ya está en uso; repite el barrido; comprueba enlace |
| Un nodo siempre offline | canal LoRa del portal distinto del que tenía el nodo; re-adóptalo, o sube el timeout |
| El PLC no lee nada | Slave ID / baud / paridad no coinciden; DE del RS-485 mal cableado; A/B invertidos |
| Escribo una coil y el relé no cambia | el relé está deshabilitado en el nodo (bit `x` en Ireg `i*16+5`); o el nodo está offline |
| Quiero mover un nodo a otro master | en este portal "Quitar" (envía RELEASE) y adóptalo en el otro |
