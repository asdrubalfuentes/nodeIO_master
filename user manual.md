# Manual de uso — Master IO (pasarela LoRa ↔ Modbus RTU)

Placa: **Heltec WiFi LoRa 32 V3**. Firmware: `Master IO gateway`.

El Master IO es una **pasarela**: por un lado sondea por radio LoRa hasta **8 nodos `nodeIO`** remotos; por el otro es un **esclavo Modbus RTU** en el puerto serie (USB o RS-485).

---

## 1. Primeros pasos

1. **Flashea uno o varios `nodeIO`**. Al no estar adoptados mostrarán **"SIN ADOPTAR"** en la OLED.
2. **Flashea el Master IO**. Sin nodos, entra automáticamente en el **portal de configuración**.
3. **Botón Builtin** (presión larga ~5 s) → entra al portal (en cualquier momento).
4. Conéctate por WiFi a `MasterIO-Setup` / `aysafi1234` y abre `http://192.168.4.1`.
5. **Descubrir nodos** → teclea dirección LoRa (1–254) + nombre para cada nodo.
6. **Guardar y reiniciar** → el Master arranca en modo normal.

---

## 2. Operación normal

**Pantalla principal** (OLED):
```
MASTER IO / gateway
Nodos online 2/2
Modbus 1 @ 19200 usb
ult. addr 1 rssi -78
```

**Botones:**
- **F1** (presión corta ~1 s) → abre **menú de nodos**
- **Botón Builtin** (presión larga ~5 s) → portal de configuración

---

## 3. Menú de nodos

Presiona **F1** corto:
```
MENU NODOS
-> Global
  Nodo 3
  Nodo 5
  Nodo 7
[F1+F2]=Enter [F2]=Salir
```

**Navegación:**
- **F1** (mant. presión) → subir
- **F2** (mant. presión) → bajar
- **F1 + F2** juntos → seleccionar nodo
- **F2** corto → salir al menú principal

---

## 4. Ver datos de un nodo

Selecciona un nodo en el menú → ves sus datos en vivo:

```
Nodo 3
AI: 1023 512 256 0
DI: 1 0 1 0
RO: 1 0 0 1
RSSI: -75 | ON
[F1/F2]=Nav [F2L]=Menu
```

**Navegación:**
- **F1** (presión corta) → nodo anterior
- **F2** (presión corta) → nodo siguiente
- **F2** (presión larga ~3 s) → volver al menú

---

## 5. Portal de configuración

Entra con **Botón Builtin** (presión larga).

**Secciones:**
- **Nodos adoptados**: activar/desactivar / quitar
- **Descubrir nodos**: *Buscar* (nodos sin adoptar) y *ROLLCALL* (reconstruir la
  tabla desde nodos ya adoptados por este gateway, p.ej. tras borrar la NVS o
  actualizar el firmware — no re-adopta, no toca los nodos)
- **LoRa**: dirección, frecuencia, potencia, tiempos
- **Modbus — transporte**: TCP (por defecto) / RTU / ambos · Unit ID · puerto TCP
- **WiFi de planta (STA)**: SSID/clave de la red donde vive el PLC, IP fija o DHCP
- **Modbus RTU (respaldo de banco)**: baud, formato, pines
- **IO local**: leer entradas/relés del Master (opcional)
- **WiFi del portal**: SSID y contraseña del SoftAP de configuración

"Guardar y reiniciar" persiste la configuración. En modo normal, si la WiFi de
planta está habilitada, la pantalla muestra `TCP <ip>:502`.

---

## 6. Valores por defecto

| Parámetro | Valor |
|---|---|
| Dirección LoRa del Master | 200 |
| LoRa | 915.0 MHz · BW 125 kHz · SF 9 · CR 4/5 · sync 0x34 · 14 dBm |
| Sondeo / timeout | 250 ms / 500 ms |
| Modbus | **TCP :502** · Unit ID 1 (RTU respaldo: 19200 · 8E1 · USB) |
| WiFi de planta (STA) | deshabilitada (hay que configurarla en el portal) |
| Ancho de pulso (relé) | 500 ms |
| WiFi portal | `MasterIO-Setup` / `aysafi1234` |

---

## 7. Problemas frecuentes

| Síntoma | Solución |
|---|---|
| "Buscar" no encuentra el nodo | el nodo ya está adoptado (usa **ROLLCALL**), o canal LoRa diferente; acerca antenas |
| Actualicé el firmware y no hay nodos | pulsa **ROLLCALL** (o se lanza solo al arrancar sin tabla); los nodos siguen adoptados |
| Un nodo siempre offline | re-adóptalo o sube el timeout en LoRa |
| La pantalla dice `TCP :502 wifi...` | no asocia a la WiFi de planta: revisa SSID/clave y cobertura |
| El PLC no lee Modbus TCP | el gateway y el PLC deben estar en el mismo segmento/VLAN; puerto 502 abierto; espera a que la pantalla muestre `TCP <ip>` |
| El PLC no lee Modbus RTU | transporte en RTU/ambos; verifica baud, paridad; DE del RS-485 |
| Escribo coil y relé no cambia | el relé está deshabilitado en el nodo, o offline |
| Master se reinicia al conectar por USB | el cable tiene DTR/RTS activos. Solución: cable USB sin DTR/RTS, o software (ver **Detalles técnicos**) |

Para más detalles técnicos, ver `firmware/README.md` en el repositorio.
