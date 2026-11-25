#!/usr/bin/env python3
"""Build CYD Chess, find the CYD's serial port, ERASE its flash, then transfer the firmware.

    python flash.py                  # build, then open the browser flasher (press CONNECT & FLASH)
    python flash.py --no-build       # reuse the last build, just open the browser flasher
    python flash.py --cli            # no browser: pick a port in the terminal, erase, flash
    python flash.py --list           # just list serial ports and exit
    python flash.py --port COM5      # skip detection (or /dev/ttyUSB0, /dev/cu.usbserial-110)
    python flash.py --no-erase       # skip the erase step
    python flash.py --monitor        # open the serial monitor afterwards

The firmware is built FIRST, so a compile error never leaves your board erased.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# USB-serial chips used on CYD boards: CH340, CP210x, FTDI, Espressif native USB
LIKELY_VIDS = {0x1A86: "CH340", 0x10C4: "CP210x", 0x0403: "FTDI", 0x303A: "Espressif"}


def pio_cmd():
    for name in ("pio", "platformio"):
        if shutil.which(name):
            return [name]
    try:
        subprocess.run([sys.executable, "-m", "platformio", "--version"],
                       check=True, capture_output=True)
        return [sys.executable, "-m", "platformio"]
    except Exception:
        return None


def list_serial_ports(pio):
    """Returns [(device, description, likely_chip_or_None)], likely boards first."""
    found = []
    try:
        from serial.tools import list_ports          # pyserial
        for p in list_ports.comports():
            found.append((p.device, p.description or "", LIKELY_VIDS.get(p.vid)))
    except ImportError:
        try:                                          # fall back to PlatformIO's own scanner
            out = subprocess.run(pio + ["device", "list", "--json-output"],
                                 capture_output=True, text=True, check=True).stdout
            for d in json.loads(out):
                chip = None
                hwid = (d.get("hwid") or "").upper()
                for vid, name in LIKELY_VIDS.items():
                    if "VID:PID=%04X" % vid in hwid:
                        chip = name
                found.append((d.get("port"), d.get("description") or "", chip))
        except Exception:
            pass
    found.sort(key=lambda t: (t[2] is None, t[0]))
    return found


def choose_port(pio):
    ports = list_serial_ports(pio)
    if not ports:
        print("No serial ports found.\n"
              "  - Use a USB cable that carries DATA (many charge-only cables don't).\n"
              "  - Windows/Mac: install the CH340 driver (the CYD uses a CH340 chip), then replug.\n"
              "  - Linux: add yourself to the 'dialout' group, then log out and in.")
        sys.exit(1)
    print("Serial ports:")
    for i, (dev, desc, chip) in enumerate(ports, 1):
        print("  %d) %-22s %s%s" % (i, dev, desc, "   <-- %s, likely the CYD" % chip if chip else ""))
    likely = [p for p in ports if p[2]]
    if len(ports) == 1 or (len(likely) == 1):
        dev = (likely[0] if len(likely) == 1 else ports[0])[0]
        print("Using %s" % dev)
        return dev
    while True:
        ans = input("Pick a port number [1-%d]: " % len(ports)).strip()
        if ans.isdigit() and 1 <= int(ans) <= len(ports):
            return ports[int(ans) - 1][0]


def run(cmd):
    print("\n$ " + " ".join(cmd), flush=True)
    code = subprocess.run(cmd, cwd=HERE).returncode
    if code != 0:
        print("\nStep failed (exit code %d). Nothing further was done." % code)
        if "upload" in cmd or "erase" in cmd:
            print("If it says 'Failed to connect': hold the BOOT button on the CYD, run again,\n"
                  "and release BOOT once you see 'Connecting...' turn into progress.")
        sys.exit(code)


def web_mode(pio, args):
    import functools
    import http.server
    import threading
    import webbrowser

    web = os.path.join(HERE, "web")
    image = os.path.join(web, "firmware", "cyd-chess-merged.bin")
    if not args.no_build:
        print("[1/2] Building firmware (first run downloads the toolchain - be patient)...")
        if os.path.exists(image):
            os.remove(image)
        run(pio + ["run"])
    if not os.path.exists(image):
        print("\nNo firmware image found at web/firmware/cyd-chess-merged.bin.\n"
              "Run without --no-build so it gets built first.")
        sys.exit(1)

    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=web)
    handler.log_message = lambda *a, **k: None
    http.server.ThreadingHTTPServer.allow_reuse_address = True
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.web_port), handler)
    url = "http://localhost:%d/" % args.web_port
    print("\n[2/2] Flasher page: " + url)
    print("      Press CONNECT & FLASH, choose the CYD's serial port, tick 'Erase device', press Install.")
    print("      (Chrome or Edge needed.)  Press Ctrl+C here when you're done.")
    threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nBye.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cli", action="store_true", help="terminal mode instead of the browser flasher")
    ap.add_argument("--no-build", action="store_true", help="browser mode: skip the build, use the last one")
    ap.add_argument("--web-port", type=int, default=8765, help="local port for the browser flasher page")
    ap.add_argument("--port", help="(--cli) serial port (default: detect and ask)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--no-erase", action="store_true", help="do not erase flash before flashing")
    ap.add_argument("--monitor", action="store_true", help="open the serial monitor when done")
    args = ap.parse_args()

    pio = pio_cmd()
    if args.list:
        ports = list_serial_ports(pio or [])
        for dev, desc, chip in ports:
            print("%-22s %s%s" % (dev, desc, "  [%s]" % chip if chip else ""))
        if not ports:
            print("(no serial ports found)")
        return
    if not pio:
        print("PlatformIO is not installed.  Install it with:\n\n    pip install platformio\n")
        sys.exit(1)

    if not args.cli:
        web_mode(pio, args)
        return

    port = args.port or choose_port(pio)
    port_args = ["--upload-port", port]

    print("\n[1/3] Building firmware (first run downloads the toolchain - be patient)...")
    run(pio + ["run"])

    if args.no_erase:
        print("\n[2/3] Skipping erase (--no-erase)")
    else:
        print("\n[2/3] ERASING the entire flash on %s (old firmware, settings and files are wiped)..." % port)
        run(pio + ["run", "-t", "erase"] + port_args)

    print("\n[3/3] Transferring CYD Chess to %s ..." % port)
    run(pio + ["run", "-t", "upload"] + port_args)

    print("\nDone! The board reboots into the chess menu.")
    print("First boot after an erase asks you to tap 3 crosshairs to calibrate the touch screen.")

    if args.monitor:
        run(pio + ["device", "monitor", "--port", port, "--baud", "115200"])


if __name__ == "__main__":
    main()
