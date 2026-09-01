# Binarios precompilados — `V1.2026.005-gw`

Placa: **Heltec WiFi LoRa 32 V3** (ESP32-S3, flash 8 MB, modo QIO @ 80 MHz).

| Fichero | Offset |
|---|---|
| `bootloader.bin` | `0x0000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

## Flashear con esptool

```sh
esptool.py --chip esp32s3 --port COM? --baud 460800 write_flash -z \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin
```

Solo la app (si el bootloader ya está):

```sh
esptool.py --chip esp32s3 --port COM? --baud 460800 write_flash -z 0x10000 firmware.bin
```

O desde el repo: `pio run -t upload`.

## Notas

- Modbus RTU sale por defecto por **USB (UART0, GPIO43/44)** con `mbUsb = true`;
  la consola de log queda desactivada en ese modo. Para RS-485 por `Serial1`,
  desmarca "Salir por USB" en el portal.
- `V1.2026.005-gw`: los coils de disparo de pulso (`b+4..7`) ya aceptan FC05
  (write single coil) sin devolver excepción; se autolimpian en el siguiente
  ciclo de publicación (~100 ms). Sin cambio de `CFG_MAGIC` respecto a `.004`.
- Actualizar desde `V1.2026.003-gw` o anterior **reinicia la tabla de nodos**
  (cambió `CFG_MAGIC` al añadir `mbUsb`).

---

## Detalles técnicos

### Pines (configurables en el portal)

| Señal | Pin por defecto | Uso |
|---|---|---|
| Modbus RX | GPIO43 (UART0) o GPIO2 (Serial1) | entrada serie |
| Modbus TX | GPIO44 (UART0) o GPIO3 (Serial1) | salida serie |
| Modbus DE/RE | GPIO4 (Serial1) | dirección RS-485; no usado en USB |
| Botón F1 (BUTTON_1) | GPIO47 | menú de nodos |
| Botón F2 (BUTTON_2) | GPIO48 | navegación |
| Botón Builtin | GPIO0 | portal (presión larga >5s) |

### Mapa Modbus

Esclavo con ID configurable (por defecto 1). Bloque por nodo `i` (0–7), base `i*16`:

**Input Registers (FC04):**
- `i*16 + 0..3`: AI1–AI4 (0–4095)
- `i*16 + 4`: DI bitfield (bits 0–3 = DI1–DI4)
- `i*16 + 5`: relay bitfield (bits 0–3 = closed, bits 8–11 = disabled)
- `i*16 + 6`: link status (0 = offline, 1 = online)
- `i*16 + 7`: RSSI en dBm (int16 con signo)
- `i*16 + 8`: segundos desde última respuesta
- `i*16 + 9`: dirección LoRa del nodo (0 = ranura vacía)

**Discrete Inputs (FC02):**
- `i*16 + 0..3`: DI1–DI4
- `i*16 + 4`: link online

**Coils (FC01/FC05/FC15):**
- `i*16 + 0..3`: consigna relé RO1–RO4 (write 1 = cerrar)
- `i*16 + 4..7`: pulso RO1–RO4 (write 1 → pulso; auto-limpia)

**Global (Input Register, base 900):**
- 900: proto marker 0x0203
- 901: nº nodos configurados
- 902: nº nodos online
- 903: IO local enabled (0/1)
- 904–907: AI1–AI4 del Master (si IO local)
- 908: DI bits del Master (si IO local)
- 909: relay bits del Master (si IO local)

### DTR/RTS issue (CP210x)

El chip CP210x del USB Heltec resetea el ESP32 al abrir el puerto si DTR/RTS van alto.
Esto causa bootloops cuando una webapp Modbus se conecta sin precauciones.

**Solución software:**
```python
import serial
ser = serial.Serial()
ser.port = "COM8"
ser.baudrate = 19200
ser.dtr = False   # ← ANTES de open()
ser.rts = False
ser.open()
time.sleep(3)     # espera reset/LoRa acquire
```

Los scripts en `test/` implementan esto automáticamente.

**Solución hardware:**
- Cable USB sin DTR/RTS conectado en el PCB
- O puentear el capacitor de reset (C9) de la placa

### Scripts de prueba

Carpeta `test/` contiene herramientas Python para ejercitar el esclavo Modbus sin PLC real:

```sh
pip install -r test/requirements.txt
python test/mb_dump.py --port COM8                      # snapshot global + nodos
python test/mb_watch.py --port COM8 --interval 1        # monitoreo continuo
python test/mb_relay.py --port COM8 --node 0 --on       # escribir coil
python test/mb_test.py --port COM8 --node 0 --relay 1   # secuencia completa
```

Flags comunes: `--baud` (def. 19200), `--parity {N,E,O}` (def. E), `--slave` (def. 1).

