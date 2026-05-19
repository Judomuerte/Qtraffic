from arduino.app_utils import *
import time
import requests
import urllib3
import math
import os
from collections import deque
from datetime import datetime

# ── Config ──────────────────────────────────────────────────────────────────
OPNSENSE_HOST   = "Opnsense IP"
OPNSENSE_KEY    = "api_key"
OPNSENSE_SECRET = "api_secret"
OPNSENSE_IFACE  = "interface_name"

POLL_INTERVAL       = 2.0    # seconds between polls
BASELINE_WINDOW     = 30     # samples to build baseline (~60 seconds)
SPIKE_PERSIST       = 2      # consecutive high polls before alarming
LOG_FILE            = "/var/log/arduino_traffic.log"

# Ratio thresholds above baseline to trigger each level
THRESH_LEVEL1 = 1.5    # slightly elevated
THRESH_LEVEL2 = 2.5    # notable
THRESH_LEVEL3 = 5.0    # suspicious
THRESH_LEVEL4 = 10.0   # alarm

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# ── State ────────────────────────────────────────────────────────────────────
prev_rx    = 0
prev_tx    = 0
prev_time  = 0.0
first_poll = True

rx_baseline = deque(maxlen=BASELINE_WINDOW)
tx_baseline = deque(maxlen=BASELINE_WINDOW)

rx_consecutive_high = 0
tx_consecutive_high = 0

# ── Logging ───────────────────────────────────────────────────────────────────
def log_spike(direction, bps, ratio, level):
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    msg = f"[{ts}] SPIKE {direction} {bps/1024:.1f} KB/s ({ratio:.1f}x baseline) level={level}"
    print(msg)
    try:
        with open(LOG_FILE, "a") as f:
            f.write(msg + "\n")
    except Exception:
        pass

# ── Adaptive scaler ───────────────────────────────────────────────────────────
def scale_traffic(bps: float, baseline: deque) -> int:
    baseline.append(bps)
    if len(baseline) < 5:
        return 0
    avg = sum(baseline) / len(baseline)
    if avg < 500:       # truly idle baseline, use absolute floor
        avg = 500
    ratio = bps / avg
    if ratio < THRESH_LEVEL1:  return 0
    if ratio < THRESH_LEVEL2:  return 1
    if ratio < THRESH_LEVEL3:  return 2
    if ratio < THRESH_LEVEL4:  return 3
    return 4

# ── Spike persistence filter ──────────────────────────────────────────────────
def persist_filter(scaled: int, counter_ref: list) -> int:
    if scaled >= 3:
        counter_ref[0] += 1
    else:
        counter_ref[0] = max(0, counter_ref[0] - 1)
    # Only show level 3-4 if sustained for SPIKE_PERSIST polls
    if scaled >= 3 and counter_ref[0] < SPIKE_PERSIST:
        return 2
    return scaled

# ── OPNsense API ─────────────────────────────────────────────────────────────
def fetch_iface_bytes():
    url = f"https://{OPNSENSE_HOST}/api/diagnostics/interface/getInterfaceStatistics"
    try:
        r = requests.get(url, auth=(OPNSENSE_KEY, OPNSENSE_SECRET),
                         verify=False, timeout=4)
        r.raise_for_status()
        stats = r.json().get("statistics", {})
        total_rx = 0
        total_tx = 0
        matched  = 0
        for key, iface in stats.items():
            if OPNSENSE_IFACE in key and isinstance(iface, dict):
                total_rx += int(iface.get("received-bytes", 0))
                total_tx += int(iface.get("sent-bytes", 0))
                matched  += 1
        if matched:
            return total_rx, total_tx
    except Exception as e:
        print(f"[opnsense] fetch error: {e}")
    return None

# ── Main loop ─────────────────────────────────────────────────────────────────
rx_counter = [0]
tx_counter = [0]

def loop():
    global prev_rx, prev_tx, prev_time, first_poll

    result = fetch_iface_bytes()
    now = time.monotonic()

    if result is not None:
        rx_bytes, tx_bytes = result

        if not first_poll:
            elapsed  = max(now - prev_time, 0.1)
            rx_rate  = max(0, (rx_bytes - prev_rx) / elapsed)
            tx_rate  = max(0, (tx_bytes - prev_tx) / elapsed)

            rx_scaled = scale_traffic(rx_rate, rx_baseline)
            tx_scaled = scale_traffic(tx_rate, tx_baseline)

            # Log genuine spikes before persistence filter
            if rx_scaled >= 3:
                avg_rx = sum(rx_baseline) / len(rx_baseline)
                log_spike("RX", rx_rate, rx_rate / max(avg_rx, 1), rx_scaled)
            if tx_scaled >= 3:
                avg_tx = sum(tx_baseline) / len(tx_baseline)
                log_spike("TX", tx_rate, tx_rate / max(avg_tx, 1), tx_scaled)

            # Apply persistence — suppress one-poll noise
            rx_display = persist_filter(rx_scaled, rx_counter)
            tx_display = persist_filter(tx_scaled, tx_counter)

            baseline_ready = len(rx_baseline) >= 5
            status = "calibrating..." if not baseline_ready else \
                     f"RX {rx_rate/1024:.1f} KB/s ({rx_display})  TX {tx_rate/1024:.1f} KB/s ({tx_display})"
            print(status)

            Bridge.call("updateTraffic", rx_display, tx_display)

        prev_rx, prev_tx = rx_bytes, tx_bytes
        prev_time = now
        first_poll = False

    time.sleep(POLL_INTERVAL)

App.run(user_loop=loop)
