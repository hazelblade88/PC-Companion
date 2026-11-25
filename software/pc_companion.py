"""
PC side for the ESP8266 companion.

Sends  : fps|cpuTemp|gpuTemp|HH:MM|DD Mon|playing|title|artist|posSec|durSec
Receives (from the touch sensor):
         CMD:PLAY / CMD:NEXT / CMD:PREV  -> Windows media keys
         CMD:SLEEP                       -> puts the PC to sleep
         CMD:SHUTDOWN                    -> shuts the PC down (after 5 seconds)

Every command received is printed in this window, so you can see whether the
ESP8266 reached the PC.

Setup (Windows):
  pip install pyserial requests
  pip install winrt-runtime winrt-Windows.Media.Control winrt-Windows.Foundation winrt-Windows.Foundation.Collections
  1. Install LibreHardwareMonitor, run it as admin, then
     Options -> Remote Web Server -> Run  (default port 8085)   [CPU/GPU temps]
  2. Install MSI Afterburner (includes RTSS) and keep it running [FPS]
  3. Set COM_PORT below (Device Manager -> Ports) and run this file.

To cancel a shutdown during the 5 seconds run:  shutdown /a
"""

import asyncio
import ctypes
from ctypes import wintypes
import mmap
import struct
import subprocess
import time
from datetime import datetime

import requests
import serial

COM_PORT = "COM7"
BAUD = 9600
LHM_URL = "http://localhost:8085/data.json"


# ---------------- Temperatures (LibreHardwareMonitor) ----------------
def _walk(node, out):
    if node.get("Type") == "Temperature" or "°C" in str(node.get("Value", "")):
        out.append((node.get("Text", ""), node.get("Value", "")))
    for child in node.get("Children", []):
        _walk(child, out)


def _num(v):
    try:
        return int(float(str(v).replace("°C", "").replace(",", ".").strip()))
    except ValueError:
        return 0


def get_temps():
    cpu = gpu = 0
    try:
        data = requests.get(LHM_URL, timeout=1).json()
        temps = []
        _walk(data, temps)
        for name, val in temps:
            n = name.lower()
            if not cpu and any(k in n for k in ("cpu package", "tctl", "tdie", "core max", "cpu")):
                cpu = _num(val)
            if not gpu and "gpu core" in n:
                gpu = _num(val)
        if not cpu:
            for name, val in temps:
                if "core" in name.lower() and "gpu" not in name.lower():
                    cpu = _num(val)
                    break
    except Exception:
        pass
    return cpu, gpu


# ---------------- FPS (RTSS shared memory) ----------------
def get_fps():
    try:
        head = mmap.mmap(0, 36, tagname="RTSSSharedMemoryV2", access=mmap.ACCESS_READ)
        sig, ver, entry_size, arr_off, arr_size = struct.unpack("<5I", head[:20])
        head.close()
        if sig != 0x52545353:  # 'RTSS'
            return 0
        total = arr_off + entry_size * arr_size
        mm = mmap.mmap(0, total, tagname="RTSSSharedMemoryV2", access=mmap.ACCESS_READ)
        best = 0
        for i in range(arr_size):
            base = arr_off + i * entry_size
            pid = struct.unpack_from("<I", mm, base)[0]
            if pid == 0:
                continue
            t0, t1, frames = struct.unpack_from("<3I", mm, base + 268)
            if t1 > t0 and frames:
                f = int(1000.0 * frames / (t1 - t0))
                best = max(best, f)
        mm.close()
        return best
    except Exception:
        return 0


# ---------------- Music (Windows media session) ----------------
try:
    from winrt.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as MediaManager,
    )
except Exception as e:
    print(
        "Music disabled - run: pip install winrt-runtime winrt-Windows.Media.Control "
        "winrt-Windows.Foundation winrt-Windows.Foundation.Collections"
    )
    print("Reason:", e)
    MediaManager = None


async def _media():
    mgr = await MediaManager.request_async()
    s = mgr.get_current_session()
    if not s:
        return 0, "", "", 0, 0
    info = await s.try_get_media_properties_async()
    playing = 1 if int(s.get_playback_info().playback_status) == 4 else 0
    pos = dur = 0
    try:
        tl = s.get_timeline_properties()
        pos = int(tl.position.total_seconds())
        dur = int(tl.end_time.total_seconds())
    except Exception:
        pass
    return playing, info.title or "", info.artist or "", max(pos, 0), max(dur, 0)


def clean(s):
    return s.encode("ascii", "ignore").decode().replace("|", " ").replace("\n", " ")[:60]


# ---------------- Media keys ----------------
VK = {"PLAY": 0xB3, "NEXT": 0xB0, "PREV": 0xB1}


def press(vk):
    ctypes.windll.user32.keybd_event(vk, 0, 0, 0)
    ctypes.windll.user32.keybd_event(vk, 0, 2, 0)


# ---------------- Power ----------------
def _tap_key(vk, hold=0.08):
    """Send a Windows virtual key press and release."""
    user32 = ctypes.windll.user32
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(hold)
    user32.keybd_event(vk, 0, 2, 0)


def pc_sleep():
    """
    Use the Windows 11 shortcut confirmed by the user:
    Win+X, then U, then S (Sleep).
    """
    print(">> SLEEP command received - using Windows power menu shortcut")

    VK_LWIN = 0x5B
    VK_X = 0x58
    VK_U = 0x55
    VK_S = 0x53
    user32 = ctypes.windll.user32

    try:
        # Open the Windows Quick Link menu (Win+X).
        user32.keybd_event(VK_LWIN, 0, 0, 0)
        time.sleep(0.12)
        _tap_key(VK_X)
        user32.keybd_event(VK_LWIN, 0, 2, 0)

        # Select Shut down or sign out, then Sleep.
        time.sleep(0.7)
        _tap_key(VK_U)
        time.sleep(0.5)
        _tap_key(VK_S)

        print("   Sent Sleep shortcut: Win+X, U, S.")
    except Exception as e:
        try:
            user32.keybd_event(VK_LWIN, 0, 2, 0)
        except Exception:
            pass
        print("   Failed to send Sleep shortcut:", e)


def pc_shutdown():
    print(">> SHUTDOWN command received - shutting down in 5 seconds (cancel: shutdown /a)")
    try:
        result = subprocess.run(
            ["shutdown", "/s", "/t", "5"],
            capture_output=True,
            text=True,
        )
        print("   shutdown exit code", result.returncode, (result.stdout + result.stderr).strip())
    except Exception as e:
        print("   Shutdown failed:", e)


# ---------------- Serial ----------------
def open_port():
    while True:
        try:
            ser = serial.Serial(COM_PORT, BAUD, timeout=0.05)
            time.sleep(2)  # ESP8266 resets when the port opens
            return ser
        except serial.SerialException as e:
            print("Waiting for", COM_PORT, "-", e)
            time.sleep(3)


def main():
    ser = open_port()
    loop = asyncio.new_event_loop()
    media = (0, "", "", 0, 0)
    last_media = last_temp = last_send = 0
    cpu = gpu = 0

    print("Running... Ctrl+C to stop")

    while True:
        try:
            # Commands coming from the ESP8266
            while ser.in_waiting:
                cmd = ser.readline().decode(errors="ignore").strip()
                if not cmd.startswith("CMD:"):
                    continue

                what = cmd[4:]
                print("From ESP8266:", cmd)

                if what in VK:
                    press(VK[what])
                    last_media = time.time() - 0.5
                elif what == "SLEEP":
                    pc_sleep()
                elif what == "SHUTDOWN":
                    pc_shutdown()

            now = time.time()

            if now - last_temp > 1:
                cpu, gpu = get_temps()
                last_temp = now

            if MediaManager and now - last_media > 1:
                try:
                    media = loop.run_until_complete(_media())
                except Exception:
                    media = (0, "", "", 0, 0)
                last_media = now

            if now - last_send >= 0.5:
                fps = get_fps()
                dt = datetime.now()
                line = "|".join([
                    str(fps),
                    str(cpu),
                    str(gpu),
                    dt.strftime("%H:%M"),
                    dt.strftime("%a %d %b"),
                    str(media[0]),
                    clean(media[1]),
                    clean(media[2]),
                    str(media[3]),
                    str(media[4]),
                ])
                ser.write((line + "\n").encode())
                last_send = now

            time.sleep(0.05)

        except (serial.SerialException, OSError) as e:
            print("Serial lost:", e, "- reconnecting")
            try:
                ser.close()
            except Exception:
                pass
            ser = open_port()


if __name__ == "__main__":
    main()
