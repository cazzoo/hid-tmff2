"""FFB Visualizer entry point.

Examples:
  # Auto-pick the first FFB-capable device and run a full proxy:
  sudo python3 main.py

  # Read-only observer (no setup, no effect params):
  python3 main.py --observe

  # Pick a specific device:
  sudo python3 main.py --device /dev/input/event12

  # Custom port:
  python3 main.py --port 8080
"""
from __future__ import annotations

import argparse
import logging
import sys

from aiohttp import web

import proxy as proxy_mod
from server import build_app


def pick_device(cli_dev: str):
    devs = proxy_mod.list_ffb_devices()
    if not devs:
        print("No FFB-capable input devices found.", file=sys.stderr)
        sys.exit(1)
    if cli_dev:
        for d in devs:
            if d["path"] == cli_dev:
                return d
        print(f"Device {cli_dev} not found among FFB devices:", file=sys.stderr)
        for d in devs:
            print(f"  {d['path']}  {d['name']}", file=sys.stderr)
        sys.exit(2)
    # Auto-pick: prefer a name mentioning wheel/thrustmaster/logitech/fanatec.
    keywords = ("wheel", "thrustmaster", "logitech", "fanatec", "t300", "t500",
                "g29", "g920", "t248")
    for d in devs:
        if any(k in d["name"].lower() for k in keywords):
            return d
    return devs[0]


def main() -> None:
    ap = argparse.ArgumentParser(description="Linux FFB wheel visualizer")
    ap.add_argument("--device", "-d", help="real device path /dev/input/eventN")
    ap.add_argument("--observe", action="store_true",
                    help="read-only mode (no proxy, no effect parameters)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s")

    dev = pick_device(args.device)
    mode = "observe" if args.observe else "proxy"
    print(f"FFB Visualizer — {mode} mode")
    print(f"  real device: {dev['path']}  ({dev['name']})")
    if not args.observe:
        print("  (virtual device node will be printed once the proxy starts)")

    app = build_app(dev["path"], args.observe)
    print(f"\n  Dashboard:  http://{args.host}:{args.port}/\n")
    web.run_app(app, host=args.host, port=args.port, print=None)


if __name__ == "__main__":
    main()
