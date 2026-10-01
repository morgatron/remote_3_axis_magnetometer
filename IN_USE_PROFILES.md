The device profiles currently in use are the following, with labels to refer to them.
All use battery power unless specified otherwise

- Heltec V4 sensor (label: SPRINGBANK)
    - connected to FLC100.
    - transmits over LoRa only
- Heltec V4 Receiver (label: ROOF)
    - Receives over LoRa (SX1262 Sub-GHz, batches from SPRINGBANK)
    - Connects directly to campus enterprise Wi-Fi (ANU-Secure WPA2-Enterprise 802.1X PEAP or standard PSK)
    - Synchronizes atomic UTC time via NTP (pool.ntp.org) and reconstructs millisecond-accurate ISO 8601 sample timestamps
    - Egress: Direct HTTP POST JSON batch forwarding to Central Server (`/api/v1/telemetry/batch` with `X-API-Key`)
    - Concurrent USB Serial CDC output preserved for local monitoring and CLI configuration
    - wired power
- Seeed Studio Xiao ESP32-C6 Sensor (label: CREEK)
    - connected to FLC100
    - communicates over BLE with coded PHY
- Seeed Studio Xiao ESP32-C6 Sensor (label: CREEK_TEST)
    - connected to FLC100
    - communicates over BLE with coded PHY (node ID: _CREEK)
- Seed Studio Xiao ESP32-C6 (label: CREEK_GATEWAY)
    - Receives using BLE coded PHY only
    - relays data to host computer using BLE
- Supermini ESP32-C3 Receiver (label: SUPERMINI_GATEWAY)
    - Receives using BLE Coded PHY only (Slotted Rendezvous: 350 ms lead time, 1000 ms fallback window, immediate discovery lock)
    - Dedicated Coded PHY active scanning (`pScan->setPhy(SCAN_CODED)`) ensuring 100% duty cycle reception and instant `AUX_SCAN_REQ` hardware ACKs
    - Cadence tracking invariant to backlog (`nominalPeriod = 10000 ms`, never scales with `sample_count`)
    - Relays data to host computer using BLE Extended Advertising (1M PHY, 200 ms burst)
    - USB-CDC disabled on boot (`ARDUINO_USB_MODE=0`, `ARDUINO_USB_CDC_ON_BOOT=0`) for minimum quiescent power draw
    - Wi-Fi radio powered off (`WiFi.mode(WIFI_OFF)`, ~80 mA saved)
    - Dynamic Frequency Scaling (DFS): 40 MHz during radio sleep (~10–12 mA), 80 MHz during burst/RX
    - Target current: ~12 - 15 mA average (~65 - 80 hours on 1,000 mAh LiPo / ~8 - 10 days on 3,000 mAh 18650)
- Seeed Studio Xiao ESP32-C6 Sensor (label: LAB_BENCH)
    - connected to FLC100
    - wired USB power (used for lab bench magnetic measurements)
    - communicates with desktop GUI host computer over USB Serial only (`MODE SERIAL`, 921600 baud)
    - 100 Hz output streaming rate (1 kHz raw ADS131E08 ADC data downsampled by a factor of 10)
    - not for use with wireless gateway or central server (BLE/LoRa/WiFi radios disabled)
- Supermini ESP32-C3 Sensor (label: LYNEHAM1)
    - connected to RM3100 3-axis magneto-inductive sensor
    - Cycle count set to 600 on X, Y, Z (gain: 221.76 LSB/µT, resolution: 4.51 nT/LSB)
    - Continuous sampling at ~40 Hz (`TMRC 0x96`), downsampled 40x via boxcar averaging to 1.00 Hz
    - `I2CEN` held LOW via GPIO 5 virtual ground to force hardware SPI mode
    - wired USB power (plugs directly into host computer)
    - communicates with host computer over USB Serial only (`MODE SERIAL`, 921600 baud, 1 Hz CSV output)
    - not for use with wireless gateway or central server (BLE/LoRa/WiFi radios disabled)



