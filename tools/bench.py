#!/usr/bin/env python3
"""PC-side client for the PicoW AI Harness bench portal on the RPi3.

Standard library only. The bench address comes from --bench or the BENCH
environment variable (default 192.168.0.198:8081).

  python tools/bench.py devices
  python tools/bench.py swd                                 # is the RP2040 reachable?
  python tools/bench.py build  firmware/temp-display
  python tools/bench.py flash  firmware/temp-display        # project dir or .elf
  python tools/bench.py deploy firmware/temp-display --until "reading 1:"
  python tools/bench.py reset --seconds 5
  python tools/bench.py log --seconds 5
  python tools/bench.py write "help\\n"
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_BENCH = "192.168.0.198:8081"


class Bench:
    def __init__(self, addr):
        if not addr.startswith("http"):
            addr = "http://" + addr
        self.base = addr.rstrip("/")

    def call(self, method, path, params=None, body=None, timeout=60):
        params = {k: v for k, v in (params or {}).items() if v is not None}
        url = self.base + path
        if params:
            url += "?" + urllib.parse.urlencode(params)
        req = urllib.request.Request(url, data=body, method=method)
        if body is not None:
            req.add_header("Content-Type", "application/octet-stream")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                data = json.load(r)
        except urllib.error.HTTPError as e:
            data = json.load(e)
        except urllib.error.URLError as e:
            sys.exit("cannot reach bench at %s: %s" % (self.base, e.reason))
        if not data.get("ok"):
            sys.exit("bench error: %s" % data.get("error"))
        return data


def find_pio():
    for cand in (shutil.which("pio"), shutil.which("platformio"),
                 os.path.expanduser("~/.platformio/penv/Scripts/pio.exe"),
                 os.path.expanduser("~/.platformio/penv/bin/pio")):
        if cand and os.path.exists(cand):
            return cand
    sys.exit("PlatformIO not found (install the VS Code PlatformIO extension or `pip install platformio`)")


def build(project, env=None):
    cmd = [find_pio(), "run", "-d", project] + (["-e", env] if env else [])
    print("$", " ".join(cmd), flush=True)
    if subprocess.call(cmd) != 0:
        sys.exit("build failed")


def resolve_image(target, env=None):
    """An .elf path is used as-is; a project dir maps to its built firmware.elf."""
    if os.path.isfile(target):
        return target
    pattern = os.path.join(target, ".pio", "build", env or "*", "firmware.elf")
    hits = sorted(glob.glob(pattern), key=os.path.getmtime)
    if not hits:
        sys.exit("no firmware.elf under %s; run `build` first" % target)
    return hits[-1]


def follow_log(bench, slot, since, seconds, grep=None, until=None):
    """Print serial lines for `seconds` (forever if 0). Returns True if `until` was seen."""
    end = time.time() + seconds if seconds else None
    while end is None or time.time() < end:
        wait = 2.0 if end is None else max(0.1, min(2.0, end - time.time()))
        r = bench.call("GET", "/api/serial/log", {"slot": slot, "since": since, "wait": wait},
                       timeout=wait + 15)
        for ln in r["lines"]:
            if grep is None or grep in ln["line"]:
                print(ln["line"], flush=True)
            if until and until in ln["line"]:
                return True
        since = r["last_seq"]
    return False


def slot_name(a):
    return a.slot or "probe"


def cmd_devices(b, a):
    for d in b.call("GET", "/api/devices")["devices"]:
        if d["present"]:
            print("%-6s %-14s %s  %s" % (d["slot"], d["device"], d["vid_pid"], d["description"]))
        else:
            print("%-6s (not attached)" % d["slot"])


def cmd_swd(b, a):
    out = b.call("GET", "/api/swd", timeout=40)["output"]
    print("\n".join(l for l in out.splitlines() if "Info" in l and "gdb" not in l))


def cmd_build(b, a):
    build(a.project, a.env)


def do_flash(b, a):
    image = resolve_image(a.target, a.env)
    with open(image, "rb") as f:
        data = f.read()
    print("flashing %s (%d bytes) over SWD ..." % (image, len(data)), flush=True)
    r = b.call("POST", "/api/flash", body=data, timeout=200)
    if a.verbose:
        print(r["output"])
    print("flashed OK")
    return r["since"]


def cmd_flash(b, a):
    do_flash(b, a)


def cmd_deploy(b, a):
    if not os.path.isfile(a.target):
        build(a.target, a.env)
    since = do_flash(b, a)
    print("--- serial output, slot %s (%ss) ---" % (slot_name(a), a.seconds), flush=True)
    found = follow_log(b, slot_name(a), since[slot_name(a)], a.seconds, until=a.until)
    if a.until and not found:
        sys.exit("expected output %r not seen" % a.until)


def cmd_reset(b, a):
    r = b.call("POST", "/api/reset")
    print("reset PicoW")
    if a.seconds:
        follow_log(b, slot_name(a), r["since"][slot_name(a)], a.seconds)


def cmd_log(b, a):
    since = 0 if a.history else b.call("GET", "/api/serial/log",
                                       {"slot": slot_name(a), "limit": 1})["last_seq"]
    found = follow_log(b, slot_name(a), since, a.seconds, a.grep, a.until)
    if a.until and not found:
        sys.exit("expected output %r not seen" % a.until)


def cmd_write(b, a):
    data = a.text.encode().decode("unicode_escape").encode("latin-1")
    r = b.call("POST", "/api/serial/write", {"slot": slot_name(a)}, body=data)
    print("wrote %d bytes to %s" % (r["bytes"], r["slot"]))


def main():
    ap = argparse.ArgumentParser(description="PicoW AI Harness bench client")
    ap.add_argument("--bench", default=os.environ.get("BENCH", DEFAULT_BENCH),
                    help="portal address host:port (env BENCH)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def slot_arg(p):
        p.add_argument("--slot", help="probe (Debug Probe UART, default) or pico (PicoW USB)")

    def flash_args(p):
        p.add_argument("target", help="PlatformIO project dir or .elf file")
        p.add_argument("--env", help="PlatformIO environment")
        p.add_argument("-v", "--verbose", action="store_true", help="show OpenOCD output")

    sub.add_parser("devices", help="list serial ports attached to the Pi").set_defaults(fn=cmd_devices)
    sub.add_parser("swd", help="check the Debug Probe can reach the RP2040").set_defaults(fn=cmd_swd)

    p = sub.add_parser("build", help="build a PlatformIO project on the PC")
    p.add_argument("project")
    p.add_argument("--env")
    p.set_defaults(fn=cmd_build)

    p = sub.add_parser("flash", help="flash an ELF through the Pi and Debug Probe")
    flash_args(p)
    p.set_defaults(fn=cmd_flash)

    p = sub.add_parser("deploy", help="build, flash, then show serial output")
    flash_args(p)
    p.add_argument("--seconds", type=float, default=8)
    p.add_argument("--until", help="stop early when this text appears; fail if it never does")
    slot_arg(p)
    p.set_defaults(fn=cmd_deploy)

    p = sub.add_parser("reset", help="reset the PicoW over SWD")
    p.add_argument("--seconds", type=float, default=0, help="then show output this long")
    slot_arg(p)
    p.set_defaults(fn=cmd_reset)

    p = sub.add_parser("log", help="show serial output")
    p.add_argument("--seconds", type=float, default=5, help="0 = follow forever")
    p.add_argument("--history", action="store_true", help="include buffered past output")
    p.add_argument("--grep", help="only lines containing this text")
    p.add_argument("--until", help="stop when this text appears; fail if it never does")
    slot_arg(p)
    p.set_defaults(fn=cmd_log)

    p = sub.add_parser("write", help="send text to the serial port (\\n escapes allowed)")
    p.add_argument("text")
    slot_arg(p)
    p.set_defaults(fn=cmd_write)

    a = ap.parse_args()
    # Serial output can contain anything; never let the Windows console codec crash us.
    sys.stdout.reconfigure(errors="replace")
    a.fn(Bench(a.bench), a)


if __name__ == "__main__":
    main()
