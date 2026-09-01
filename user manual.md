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
- **Descubrir nodos**: buscar y adoptar nuevos
- **LoRa**: dirección, frecuencia, potencia, tiempos
- **Modbus RTU**: Slave ID, baud, formato, pines
- **IO local**: leer entradas/relés del Master (opcional)
- **WiFi**: SSID y contraseña del portal

"Guardar y reiniciar" persiste la configuración.

---

## 6. Valores por defecto

| Parámetro | Valor |
|---|---|
| Dirección LoRa del Master | 200 |
| LoRa | 915.0 MHz · BW 125 kHz · SF 9 · CR 4/5 · sync 0x34 · 14 dBm |
| Sondeo / timeout | 250 ms / 500 ms |
| Modbus | Slave 1 · 19200 · 8E1 · por USB |
| Ancho de pulso (relé) | 500 ms |
| WiFi portal | `MasterIO-Setup` / `aysafi1234` |

---

## 7. Problemas frecuentes

| Síntoma | Solución |
|---|---|
| "Buscar" no encuentra el nodo | el nodo está adoptado en otro master, o canal LoRa diferente; acerca antenas |
| Un nodo siempre offline | re-adóptalo o sube el timeout en LoRa |
| El PLC no lee Modbus | verifica Slave ID, baud, paridad; DE del RS-485 mal cableado |
| Escribo coil y relé no cambia | el relé está deshabilitado en el nodo, o offline |
| Master se reinicia al conectar por USB | el cable tiene DTR/RTS activos. Solución: cable USB sin DTR/RTS, o software (ver **Detalles técnicos**) |

Para más detalles técnicos, ver `firmware/README.md` en el repositorio.
