# Hardware & Low-Power Telemetry Test Suite

This directory contains automated test suites to verify firmware functionality, C-struct binary serialization, CLI commands, and low-power telemetry power management across both host environments and physical single-device hardware setups.

---

## 1. Test Suite Summary

| Test Script | Target | Requires Hardware? | Description |
| :--- | :--- | :--- | :--- |
| `test/test_batch_serialization.py` | Host PC | **NO** | Validates 139-byte `SensorBatchPacket` struct memory alignment, compact sample packing (`CompactSample`), and microsecond relative timestamp reconstruction logic. |
| `test/test_receiver_parser.py` | Host PC | **NO** | Validates CSV parsing and timestamp back-calculation via `stream_parser.py`. |
| `test/test_scaling_math.py` | Host PC | **NO** | Verifies on-board scale factor math invariance across dynamic cycle counts. |
| `test/test_sensor_standalone.py` | Sensor Node | **YES** (`/dev/ttyACM0`) | Verifies single sensor node CLI commands (`BATCH 1..10`, `STATUS`), NVS Flash persistence, and sub-microsecond `DRDY` interrupt timestamp jitter under DFS. |
| `test/test_receiver_standalone.py` | Receiver Node | **YES** (`/dev/ttyACM1`) | Verifies single gateway receiver node CLI commands (`STATUS`, `NODES`, `MODE`), 80 MHz CPU scaling, and active node tracking. |
| `test/test_integration_sensor_receiver.py` | Sensor + Receiver | **YES** (`/dev/ttyACM0` + `/dev/ttyACM1`) | Verifies end-to-end Bluetooth 5.0 LE Coded PHY Extended Advertising batching (`BATCH 10` vs `BATCH 5`), hardware `AUX_SCAN_REQ` ACKs, and 10-minute disconnect ring buffer catch-up flushing. |

---

## 2. Running the Tests

### Host Unit Tests (No Hardware Required)
```bash
python -m unittest test/test_batch_serialization.py
python -m unittest test/test_receiver_parser.py
python test/test_scaling_math.py
```

### Standalone Single-Device Hardware Tests
```bash
# Sensor Node Hardware Self-Test (single device on /dev/ttyACM0)
python test/test_sensor_standalone.py --port /dev/ttyACM0

# Receiver Node Hardware Self-Test (single device on /dev/ttyACM1)
python test/test_receiver_standalone.py --port /dev/ttyACM1
```

### End-to-End Multi-Device Integration Test
```bash
# End-to-End Batching & Catch-up Flush Test (Sensor on /dev/ttyACM0, Receiver on /dev/ttyACM1)
python test/test_integration_sensor_receiver.py --sensor-port /dev/ttyACM0 --rcvr-port /dev/ttyACM1
```

### Full Automated Test Suite Execution
```bash
python test/test_batch_serialization.py && \
python test/test_sensor_standalone.py --port /dev/ttyACM0 && \
python test/test_receiver_standalone.py --port /dev/ttyACM1 && \
python test/test_integration_sensor_receiver.py --sensor-port /dev/ttyACM0 --rcvr-port /dev/ttyACM1
```
