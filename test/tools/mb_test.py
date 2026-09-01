#!/usr/bin/env python3
"""End-to-end Modbus check on a single serial connection (one board reset).

    python tools/mb_test.py --port COM8 --node 0 --relay 1

Sequence: snapshot -> close relay -> wait a poll -> snapshot -> pulse relay
-> snapshot -> open relay -> snapshot. Reads AI/DI/relay/link each time.
"""
from __future__ import annotations

import argparse
import time

from mb_lib import Gateway, add_serial_args, open_client


def show(gw: Gateway, slot: int, tag: str) -> None:
    g = gw.globals()
    n = gw.node(slot, with_bits=True)
    di = "".join(map(str, n.di))
    ro = "".join(map(str, n.relay_on))
    sp = "".join(map(str, n.coil_setpoint))
    pl = "".join(map(str, n.coil_pulse))
    print(f"[{tag:<14}] marker=0x{g.marker:04X} nodes={g.online}/{g.node_count} | "
          f"slot{slot} addr{n.addr} {'UP' if n.online else 'DOWN'} "
          f"AI={n.ai} DI={di} RO={ro} disabled={''.join(map(str,n.relay_dis))} "
          f"coilSP={sp} coilPulse={pl} rssi={n.rssi} age={n.age_s}s")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    add_serial_args(ap)
    ap.add_argument("--node", type=int, default=0, choices=range(8), metavar="0..7")
    ap.add_argument("--relay", type=int, default=1, choices=[1, 2, 3, 4])
    ap.add_argument("--poll-wait", type=float, default=1.5,
                    help="seconds to wait for the LoRa poll to carry a write (default 1.5)")
    a = ap.parse_args()

    c = open_client(a)
    gw = Gateway(c, a.slave)
    try:
        print(f"# {a.port} {a.baud} 8{a.parity}{a.stopbits} slave {a.slave} "
              f"node {a.node} relay {a.relay}")
        show(gw, a.node, "baseline")

        gw.set_relay(a.node, a.relay, True)
        print(f"  -> write coil {a.node*16 + (a.relay-1)} = 1 (close)")
        time.sleep(a.poll_wait)
        show(gw, a.node, "after close")

        gw.pulse_relay(a.node, a.relay)
        print(f"  -> write coil {a.node*16 + 4 + (a.relay-1)} = 1 (pulse)")
        time.sleep(a.poll_wait)
        show(gw, a.node, "after pulse")

        gw.set_relay(a.node, a.relay, False)
        print(f"  -> write coil {a.node*16 + (a.relay-1)} = 0 (open)")
        time.sleep(a.poll_wait)
        show(gw, a.node, "after open")
        return 0
    except IOError as e:
        print(f"\nMODBUS ERROR: {e}")
        return 1
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
