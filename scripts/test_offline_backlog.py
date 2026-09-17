#!/usr/bin/env python3
"""
scripts/test_offline_backlog.py

Automated integration test for Receiver Offline & Backlog Recovery.
Simulates receiver going offline (halted in ROM bootloader mode), verifies that
the sensor accumulates un-ACKed telemetry in its ring buffer, brings the receiver
back online, and verifies that the receiver re-acquires synchronization, ACKs incoming
bursts, and drains the accumulated backlog without loss of sync.
"""

import sys
import os
import time
import re
import queue
import threading
import subprocess
import asyncio
from datetime import datetime

try:
    import serial
except ImportError:
    print("Error: 'pyserial' is required. Install via: pip install pyserial")
    sys.exit(1)

try:
    from bleak import BleakScanner
    from bleak.backends.device import BLEDevice
    from bleak.backends.scanner import AdvertisementData
    HAVE_BLEAK = True
except ImportError:
    HAVE_BLEAK = False
    print("Warning: 'bleak' not available; will test via serial logs only.")

COMPANY_ID = 0xFFFF

class OfflineBacklogTest:
    def __init__(self, sensor_port="/dev/ttyACM0", receiver_port="/dev/ttyACM1"):
        self.sensor_port = sensor_port
        self.receiver_port = receiver_port
        self.sensor_events = []
        self.ble_events = []
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        
    def log(self, tag, msg):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        print(f"[{ts}] [{tag}] {msg}", flush=True)

    def sensor_serial_worker(self):
        try:
            ser = serial.Serial(self.sensor_port, 115200, timeout=0.5)
            self.log("SENSOR", f"Opened {self.sensor_port} at 115200 baud")
        except Exception as e:
            self.log("SENSOR", f"Failed to open {self.sensor_port}: {e}")
            return

        re_acked = re.compile(r"\[TX\]\s+Burst\s+ACKED!\s+Cleared\s+(\d+)\s+samples\.")
        re_nacked = re.compile(r"\[TX\]\s+Burst\s+NOT\s+ACKED\s+\(timeout\)\.\s+Retry\s+count:\s+(\d+)\.\s+Backlog:\s+(\d+)")

        while not self.stop_event.is_set():
            try:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if not line:
                    continue
                now = time.time()
                m_acked = re_acked.search(line)
                if m_acked:
                    cleared = int(m_acked.group(1))
                    ev = {"time": now, "type": "ACKED", "cleared": cleared, "raw": line}
                    with self.lock:
                        self.sensor_events.append(ev)
                    self.log("SENSOR_TX", f"ACKED: Cleared {cleared} samples")
                    continue

                m_nacked = re_nacked.search(line)
                if m_nacked:
                    retry = int(m_nacked.group(1))
                    backlog = int(m_nacked.group(2))
                    ev = {"time": now, "type": "NACK_TIMEOUT", "retry": retry, "backlog": backlog, "raw": line}
                    with self.lock:
                        self.sensor_events.append(ev)
                    self.log("SENSOR_TX", f"NACK (TIMEOUT): Retry {retry}, Backlog {backlog} samples")
                    continue

                # Other lines
                if "[TX]" in line or "SYS" in line or "ERR" in line:
                    self.log("SENSOR_LOG", line)
            except Exception as e:
                if not self.stop_event.is_set():
                    self.log("SENSOR", f"Serial read error: {e}")
                break

        ser.close()
        self.log("SENSOR", "Serial worker stopped.")

    def ble_scanner_worker(self):
        if not HAVE_BLEAK:
            return

        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)

        def callback(device: BLEDevice, adv_data: AdvertisementData):
            if COMPANY_ID not in adv_data.manufacturer_data:
                return
            raw = adv_data.manufacturer_data[COMPANY_ID]
            if len(raw) < 27 or raw[:2] != b"MG":
                return
            seq = raw[2]
            node_id = raw[3:11].split(b"\x00", 1)[0].decode("utf-8", errors="ignore")
            sample_count = raw[21]
            now = time.time()
            ev = {
                "time": now,
                "seq": seq,
                "node_id": node_id,
                "sample_count": sample_count,
                "rssi": adv_data.rssi
            }
            with self.lock:
                self.ble_events.append(ev)
            if sample_count > 0:
                self.log("BLE_GATEWAY", f"Relayed Batch: Node={node_id}, Seq={seq}, Samples={sample_count}, RSSI={adv_data.rssi}dBm")
            else:
                self.log("BLE_GATEWAY", f"Diag/Heartbeat: Node={node_id}, Seq={seq}, RSSI={adv_data.rssi}dBm")

        async def run_scan():
            scanner = BleakScanner(detection_callback=callback)
            await scanner.start()
            while not self.stop_event.is_set():
                await asyncio.sleep(0.2)
            await scanner.stop()

        try:
            loop.run_until_complete(run_scan())
        except Exception as e:
            if not self.stop_event.is_set():
                self.log("BLE", f"Bleak scan error: {e}")
        finally:
            loop.close()
            self.log("BLE", "Scanner stopped.")

    def run_test(self):
        print("=" * 78)
        print("   STARTING OFFLINE & BACKLOG RECOVERY INTEGRATION TEST")
        print("=" * 78)

        t_sensor = threading.Thread(target=self.sensor_serial_worker, daemon=True)
        t_ble = threading.Thread(target=self.ble_scanner_worker, daemon=True)
        t_sensor.start()
        t_ble.start()

        # ----------------------------------------------------
        # PHASE 1: Baseline Verification
        # ----------------------------------------------------
        self.log("TEST", "Phase 1: Verifying baseline steady-state operation (waiting for ACK)...")
        t0 = time.time()
        baseline_acked = False
        while time.time() - t0 < 25:
            with self.lock:
                acks = [e for e in self.sensor_events if e["type"] == "ACKED"]
                if len(acks) >= 1:
                    baseline_acked = True
                    break
            time.sleep(0.5)

        if not baseline_acked:
            self.log("TEST", "FAIL: Did not observe baseline ACK within 25s!")
            self.stop_event.set()
            return False

        self.log("TEST", "Phase 1: Baseline confirmed healthy. Receiver is actively ACKing bursts.")
        time.sleep(2.0)

        # ----------------------------------------------------
        # PHASE 2: Take Receiver Offline
        # ----------------------------------------------------
        self.log("TEST", "=================================================================")
        self.log("TEST", "Phase 2: Taking Receiver OFFLINE via ROM Bootloader Halt")
        self.log("TEST", "=================================================================")

        # Spawning helper python using platformio esptool
        penv_python = os.path.expanduser("~/.platformio/penv/bin/python")
        if not os.path.exists(penv_python):
            penv_python = sys.executable

        halt_cmd = [
            penv_python, "-u", "-c",
            f"import esptool, sys, time; esp = esptool.cmds.detect_chip(port='{self.receiver_port}'); print('HALTED', flush=True); sys.stdin.readline(); esp.hard_reset(); print('RESUMED', flush=True)"
        ]

        proc = subprocess.Popen(
            halt_cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        # Wait for "HALTED"
        halted = False
        t_halt_start = time.time()
        while time.time() - t_halt_start < 10:
            line = proc.stdout.readline().strip()
            if line:
                self.log("ESPTOOL", line)
            if "HALTED" in line:
                halted = True
                break

        if not halted:
            self.log("TEST", "FAIL: Failed to halt receiver into bootloader!")
            proc.kill()
            self.stop_event.set()
            return False

        self.log("TEST", "Receiver is HALTED. Holding offline for 30 seconds...")
        offline_start = time.time()
        time.sleep(30.0)
        offline_end = time.time()

        # Check timeouts during offline period
        with self.lock:
            offline_nacks = [e for e in self.sensor_events if e["type"] == "NACK_TIMEOUT" and e["time"] >= offline_start]

        self.log("TEST", f"Offline period complete. Observed {len(offline_nacks)} burst timeouts on sensor.")
        if len(offline_nacks) < 2:
            self.log("TEST", f"Warning: Expected >=2 burst timeouts, got {len(offline_nacks)}")

        # ----------------------------------------------------
        # PHASE 3: Bring Receiver Back Online
        # ----------------------------------------------------
        self.log("TEST", "=================================================================")
        self.log("TEST", "Phase 3: Bringing Receiver BACK ONLINE via Hardware Reset")
        self.log("TEST", "=================================================================")
        t_online = time.time()
        try:
            proc.stdin.write("\n")
            proc.stdin.flush()
            for line in proc.stdout:
                line = line.strip()
                if line:
                    self.log("ESPTOOL", line)
            proc.wait(timeout=5)
        except Exception as e:
            self.log("TEST", f"Exception releasing esptool: {e}")

        self.log("TEST", "Receiver hard-reset complete. Now monitoring backlog drain and re-sync...")

        # ----------------------------------------------------
        # PHASE 4: Monitor Backlog Drain & Re-Sync
        # ----------------------------------------------------
        t_recovery_start = time.time()
        max_recovery_wait = 85.0 # wait up to ~8 sensor bursts
        drained = False
        burst_after_online = 0
        drain_bursts = []

        while time.time() - t_recovery_start < max_recovery_wait:
            time.sleep(0.5)
            with self.lock:
                post_online_acks = [e for e in self.sensor_events if e["type"] == "ACKED" and e["time"] >= t_online]
            
            if len(post_online_acks) > burst_after_online:
                new_acks = post_online_acks[burst_after_online:]
                burst_after_online = len(post_online_acks)
                for a in new_acks:
                    drain_bursts.append(a)
                    self.log("TEST", f"--> Post-online ACK #{len(drain_bursts)}: Cleared {a['cleared']} samples")

                # Backlog is considered drained when we see at least one high-capacity drain burst (>10)
                # followed by a burst that clears nominal (10 or fewer) samples
                if len(drain_bursts) >= 2 and any(b['cleared'] > 10 for b in drain_bursts):
                    if drain_bursts[-1]['cleared'] <= 10:
                        drained = True
                        self.log("TEST", "Backlog drain complete! Steady-state restored.")
                        break

        # ----------------------------------------------------
        # PHASE 5: Evaluation & Report
        # ----------------------------------------------------
        self.stop_event.set()
        time.sleep(1.0)

        print("\n" + "=" * 78)
        print("                       TEST SUMMARY REPORT")
        print("=" * 78)

        with self.lock:
            all_sensor = list(self.sensor_events)
            all_ble = list(self.ble_events)

        print(f"Total Sensor Events Captured : {len(all_sensor)}")
        print(f"Total BLE Packets Relayed    : {len(all_ble)}")
        print(f"Offline Timeouts Observed    : {len(offline_nacks)}")
        print(f"Post-Online ACKs Observed    : {len(drain_bursts)}")

        print("\nSensor Events Timeline:")
        print(f"{'Time':<12} | {'Event Type':<15} | {'Details':<45}")
        print("-" * 78)
        for ev in all_sensor:
            t_rel = ev['time'] - t0
            t_str = f"+{t_rel:6.1f}s"
            if ev['type'] == 'ACKED':
                details = f"Cleared {ev['cleared']} samples (ACK received)"
            else:
                details = f"Timeout! Retry={ev['retry']}, Backlog={ev['backlog']} samples"
            print(f"{t_str:<12} | {ev['type']:<15} | {details:<45}")

        print("-" * 78)
        if all_ble:
            print("\nBLE Gateway Relayed Batches:")
            print(f"{'Time':<12} | {'Node ID':<10} | {'Seq':<5} | {'Samples':<8} | {'RSSI':<8}")
            print("-" * 78)
            for b in all_ble:
                if b['sample_count'] > 0:
                    t_rel = b['time'] - t0
                    print(f"+{t_rel:6.1f}s     | {b['node_id']:<10} | {b['seq']:<5} | {b['sample_count']:<8} | {b['rssi']:<5}dBm")

        print("=" * 78)
        # Criteria checks
        p1 = baseline_acked
        p2 = len(offline_nacks) >= 2
        p3 = any(b['cleared'] > 10 for b in drain_bursts)
        p4 = drained or len(drain_bursts) >= 3

        print(f"Criteria 1: Baseline sync & ACK operation     : {'PASS' if p1 else 'FAIL'}")
        print(f"Criteria 2: Offline burst timeouts detected   : {'PASS' if p2 else 'FAIL'} ({len(offline_nacks)} timeouts)")
        print(f"Criteria 3: High-capacity drain burst (>10)   : {'PASS' if p3 else 'FAIL'}")
        print(f"Criteria 4: Re-synchronization & full drain   : {'PASS' if p4 else 'FAIL'}")

        overall = p1 and p2 and p3 and p4
        print(f"\nOVERALL RESULT: {'SUCCESS' if overall else 'FAILURE'}")
        print("=" * 78)
        return overall

if __name__ == "__main__":
    test = OfflineBacklogTest()
    success = test.run_test()
    sys.exit(0 if success else 1)
