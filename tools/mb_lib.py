"""Shared helpers to talk to the nodeIO_master Modbus RTU gateway.

Register map (see src/modbus_gw.h and "user manual.md" section 4):

  Per node slot i = 0..7, base b = i*16
    Input Register (FC04)
      b+0..3  AI1..AI4                (0..4095)
      b+4     DI bitfield             (bit0..3 = DI1..DI4)
      b+5     relay bitfield          (bit0..3 = closed, bit8..11 = disabled 'x')
      b+6     link                    (0 = offline, 1 = online)
      b+7     RSSI dBm                (int16, signed)
      b+8     seconds since last reply
      b+9     assigned LoRa address   (0 = empty slot)
    Discrete Input (FC02)  b+0..3 DI1..DI4 ; b+4 link online
    Coil (FC01/05/15)      b+0..3 relay setpoint RO1..RO4 (write 1 = close)
                           b+4..7 pulse trigger  RO1..RO4 (write 1 -> pulse, auto-clears)

  Global Input Register
    900  proto marker 0x0203
    901  configured node count
    902  online node count
    903  local IO enabled (0/1)
    904..907  local AI1..AI4   (only if local IO enabled)
    908  local DI bits         (only if local IO enabled)
    909  local relay bits      (only if local IO enabled)
  Global Coil
    900..903  local relays     (only if local IO enabled)
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field

from pymodbus.client import ModbusSerialClient

MAX_NODES = 8
NODE_BLK = 16
GLOBAL_BASE = 900
PROTO_MARKER = 0x0203


def s16(v: int) -> int:
    """Interpret a 16-bit register as a signed int16."""
    return v - 0x10000 if v & 0x8000 else v


def bits4(v: int) -> list[int]:
    return [(v >> k) & 1 for k in range(4)]


# --------------------------------------------------------------------------- CLI
def add_serial_args(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("--port", default="COM8", help="serial port (default COM8)")
    ap.add_argument("--baud", type=int, default=19200, help="baud (default 19200)")
    ap.add_argument("--parity", default="E", choices=["N", "E", "O"],
                    help="parity, must match mbFormat (default E = 8E1)")
    ap.add_argument("--stopbits", type=int, default=1, choices=[1, 2])
    ap.add_argument("--slave", type=int, default=1, help="Modbus slave id (default 1)")
    ap.add_argument("--timeout", type=float, default=1.0)


def client_from_args(a: argparse.Namespace) -> ModbusSerialClient:
    return ModbusSerialClient(
        port=a.port,
        baudrate=a.baud,
        bytesize=8,
        parity=a.parity,
        stopbits=a.stopbits,
        timeout=a.timeout,
    )


# ----------------------------------------------------------------------- decode
@dataclass
class NodeView:
    slot: int
    addr: int
    online: bool
    ai: list[int]
    di: list[int]
    relay_on: list[int]
    relay_dis: list[int]
    rssi: int
    age_s: int
    di_discrete: list[int] = field(default_factory=list)
    coil_setpoint: list[int] = field(default_factory=list)
    coil_pulse: list[int] = field(default_factory=list)

    @property
    def present(self) -> bool:
        return self.addr != 0


@dataclass
class GlobalView:
    marker: int
    node_count: int
    online: int
    local_io: bool
    local_ai: list[int]
    local_di: list[int]
    local_relay: list[int]

    @property
    def marker_ok(self) -> bool:
        return self.marker == PROTO_MARKER


class Gateway:
    def __init__(self, client: ModbusSerialClient, slave: int = 1):
        self.c = client
        self.slave = slave

    # -- raw reads -------------------------------------------------------
    def _iregs(self, addr: int, count: int) -> list[int]:
        rr = self.c.read_input_registers(addr, count, slave=self.slave)
        if rr.isError():
            raise IOError(f"read_input_registers({addr},{count}) -> {rr}")
        return list(rr.registers)

    def _discretes(self, addr: int, count: int) -> list[int]:
        rr = self.c.read_discrete_inputs(addr, count, slave=self.slave)
        if rr.isError():
            raise IOError(f"read_discrete_inputs({addr},{count}) -> {rr}")
        return [int(b) for b in rr.bits[:count]]

    def _coils(self, addr: int, count: int) -> list[int]:
        rr = self.c.read_coils(addr, count, slave=self.slave)
        if rr.isError():
            raise IOError(f"read_coils({addr},{count}) -> {rr}")
        return [int(b) for b in rr.bits[:count]]

    # -- writes -------------------------------------------------------
    def set_relay(self, slot: int, relay1: int, on: bool) -> None:
        addr = slot * NODE_BLK + (relay1 - 1)
        rr = self.c.write_coil(addr, bool(on), slave=self.slave)
        if rr.isError():
            raise IOError(f"write_coil({addr},{on}) -> {rr}")

    def pulse_relay(self, slot: int, relay1: int) -> None:
        addr = slot * NODE_BLK + 4 + (relay1 - 1)
        rr = self.c.write_coil(addr, True, slave=self.slave)
        if rr.isError():
            raise IOError(f"write_coil({addr},pulse) -> {rr}")

    # -- decoded views -------------------------------------------------
    def node(self, slot: int, with_bits: bool = True) -> NodeView:
        b = slot * NODE_BLK
        r = self._iregs(b, 10)
        n = NodeView(
            slot=slot,
            addr=r[9],
            online=bool(r[6]),
            ai=r[0:4],
            di=bits4(r[4]),
            relay_on=bits4(r[5]),
            relay_dis=[(r[5] >> (8 + k)) & 1 for k in range(4)],
            rssi=s16(r[7]),
            age_s=r[8],
        )
        if with_bits and n.present:
            d = self._discretes(b, 5)
            n.di_discrete = d[0:4]
            co = self._coils(b, 8)
            n.coil_setpoint = co[0:4]
            n.coil_pulse = co[4:8]
        return n

    def nodes(self, with_bits: bool = True) -> list[NodeView]:
        return [self.node(i, with_bits) for i in range(MAX_NODES)]

    def globals(self) -> GlobalView:
        r = self._iregs(GLOBAL_BASE, 10)
        return GlobalView(
            marker=r[0],
            node_count=r[1],
            online=r[2],
            local_io=bool(r[3]),
            local_ai=r[4:8],
            local_di=bits4(r[8]),
            local_relay=bits4(r[9]),
        )
