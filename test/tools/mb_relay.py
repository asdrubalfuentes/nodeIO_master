#!/usr/bin/env python3
"""Drive a node relay through the gateway coils.

    python tools/mb_relay.py --port COM8 --node 0 --relay 1 --on
    python tools/mb_relay.py --port COM8 --node 0 --relay 1 --off
    python tools/mb_relay.py --port COM8 --node 0 --relay 2 --pulse

--node is the slot (0..7). Coil b+0..3 = relay setpoint, b+4..7 = pulse trigger
(auto-clears; pulse width is mcfg.pulseMs on the gateway).
"""
from __future__ import annotations

import argparse
import sys
import time

from mb_lib import Gateway, add_serial_args, open_client


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    add_serial_args(ap)
    ap.add_argument("--node", type=int, required=True, choices=range(8), metavar="0..7")
    ap.add_argument("--relay", type=int, required=True, choices=[1, 2, 3, 4])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--on", action="store_true")
    g.add_argument("--off", action="store_true")
    g.add_argument("--pulse", action="store_true")
    a = ap.parse_args()

    c = open_client(a)
    gw = Gateway(c, a.slave)
    try:
        if a.pulse:
            gw.pulse_relay(a.node, a.relay)
            action = "pulse"
        else:
            gw.set_relay(a.node, a.relay, a.on)
            action = "on" if a.on else "off"
        print(f"node {a.node} relay {a.relay} -> {action}  (queued as LoRa WR/WP)")

        time.sleep(0.5)
        n = gw.node(a.node)
        print(f"readback: link={'up' if n.online else 'down'} "
              f"RO={''.join(map(str, n.relay_on))} "
              f"disabled={''.join(map(str, n.relay_dis))} age={n.age_s}s")
        print("note: the change lands after the next LoRa poll of that node; "
              "run mb_dump.py again in a moment.")
        return 0
    except IOError as e:
        print(f"MODBUS ERROR: {e}", file=sys.stderr)
        return 1
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
