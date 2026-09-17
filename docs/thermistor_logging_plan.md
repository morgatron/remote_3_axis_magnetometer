# Implementation Plan: ADC Channel 4 Thermistor Logging (CREEK)

> [!NOTE]
> **Status: FULLY IMPLEMENTED & LIVE IN FIRMWARE**
> Thermistor logging on ADC Channel 4 is fully integrated across `FLC100_ADS131`, `TelemetryPacket.h` (`temp_c_x100`), `IngestionPipeline`, `RelayEgress`, and `scripts/ble_gateway.py`. This document is preserved as the hardware specification and Steinhart-Hart equation reference.

## 1. Overview & Objectives
On the CREEK sensor board (FLC100 3-Axis Fluxgate Carrier with TI ADS131E08 24-bit ADC), an NTC thermistor is wired to **ADC Channel 4**. 

Magnetic fluxgate sensors exhibit temperature-dependent offset drift (typically $\approx 0.5 \text{ to } 1.5\text{ nT}/^\circ\text{C}$). Logging board temperature enables calibration and thermal drift compensation.

### Key Requirements
- **Low Cadence**: Temperature changes slowly (thermal inertia of board/sensors). Sampling once every 10 seconds (or once per batch burst) is sufficient.
- **Zero RF & SPI Waste**: Clocks out Channel 4 within the existing ADS131E08 frame without extra SPI transactions. Encodes temperature in only 2 bytes in the batch header.
- **Pipeline Integration**: Ingestion pipeline (`IngestionPipeline.cpp`) already defines `item.temp` (currently defaulted to `0.0f`). This plan populates that field with real temperature data.

---

## 2. Hardware Architecture & ADC Mechanics

### ADS131E08 Channel Mapping
The ADS131E08 clocks out 27 bytes per DRDY frame (3 status bytes + 8 channels $\times$ 3 bytes):
```
[Byte 0..2]   : Status Word (24 bits, e.g. 0xC00000)
[Byte 3..5]   : CH1 (X-axis fluxgate)
[Byte 6..8]   : CH2 (Y-axis fluxgate)
[Byte 9..11]  : CH3 (Z-axis fluxgate)
[Byte 12..14] : CH4 (NTC Thermistor Divider)  <-- TARGET
[Byte 15..26] : CH5 to CH8 (Unused, powered down)
```

- **Current State**: `CH4SET` is written to `0x81` (Channel powered down, inputs shorted internally).
- **Proposed State**: Configure `CH4SET` to `0x10` (Channel powered on, Gain = 1, normal electrode input).
- **SPI Overhead**: **0 additional SPI bytes** and **0 additional interrupt latency** because the 27-byte transaction already transfers all 8 channel slots.

---

## 3. Protocol & Data Structure Specifications

### A. `SensorBatchPacket` (`include/TelemetryPacket.h`)
Add `int16_t temp_c_x100` (centi-degrees Celsius, e.g., `2350` = $23.50\,^\circ\text{C}$, `0x7FFF` = invalid/unmeasured) immediately after `vbat_mv`:

```cpp
typedef struct __attribute__((packed)) {
    char          device_id[8];         // Null-terminated identifier (e.g. "CREEK")
    uint32_t      latest_sample_age_ms; // Age in ms of newest sample at TX
    uint16_t      sample_interval_ms;   // Time between samples (e.g. 1000 ms = 1 Hz)
    uint8_t       sample_count;         // Number of samples in batch (up to 18)
    uint16_t      status;               // Status word
    uint16_t      vbat_mv;              // Battery voltage in mV
    int16_t       temp_c_x100;          // Temperature in centi-deg C (2 bytes) <-- NEW
    CompactSample samples[18];          // Array of up to 18 compact samples
} SensorBatchPacket;
```
- **Packet Size**: Increases header from 19 bytes to 21 bytes (+2 bytes). Total packet size for 10 samples increases from 139 bytes to 141 bytes (well below the 251-byte Extended Advertising limit).

### B. `GatewayAdvPacket` (`include/TelemetryPacket.h`)
Update the gateway re-broadcast packet to include `temp_c_x100`:
```cpp
typedef struct __attribute__((packed)) {
    uint16_t      company_id;           // 0xFFFF
    uint8_t       magic[2];             // 0x4D, 0x47 ("MG")
    uint8_t       packet_seq;           // Transmission sequence counter
    char          node_id[8];           // Field node identifier
    uint64_t      timestamp_us;         // Microsecond timestamp
    uint16_t      sample_interval_ms;   // Sample interval in ms
    uint8_t       sample_count;         // Number of samples in packet
    uint16_t      status;               // Status word
    uint16_t      vbat_mv;              // Battery voltage in mV
    int16_t       temp_c_x100;          // Temperature in centi-deg C (2 bytes) <-- NEW
    int8_t        rssi;                 // Signal strength in dBm
    CompactSample samples[18];          // Samples array
} GatewayAdvPacket;
```

---

## 4. Thermistor Mathematics (Beta Equation)

### Circuit Topology (Voltage Divider)
Assuming an NTC thermistor in a divider configuration:
- Reference Voltage: $V_{ref} = 2.40\text{ V}$ (or $V_{cc} = 3.3\text{ V}$)
- Bias Resistor: $R_b = 10.0\text{ k}\Omega$
- Thermistor Nominal: $R_0 = 10.0\text{ k}\Omega$ at $T_0 = 298.15\text{ K}$ ($25.0\,^\circ\text{C}$)
- Thermistor Beta: $B \approx 3950\text{ K}$ (configurable)

### Calculation Steps
1. **Raw ADC to Voltage**:
   $$\text{rawCH4} = (\text{buffer}[12] \ll 16) \mid (\text{buffer}[13] \ll 8) \mid \text{buffer}[14]$$
   Sign-extend 24-bit two's complement to 32-bit `int32_t`.
   $$V_{ch4} = \frac{\text{rawCH4} \times V_{ref}}{2^{23} \times \text{Gain}}$$

2. **Voltage to Resistance** (for high-side NTC, low-side $R_b$):
   $$R_{ntc} = R_b \times \left(\frac{V_{ref}}{V_{ch4}} - 1.0\right)$$
   *(Note: verify whether NTC is on high or low side of the carrier board divider).*

3. **Resistance to Temperature ($\beta$ parameter model)**:
   $$\frac{1}{T} = \frac{1}{T_0} + \frac{1}{B} \ln\left(\frac{R_{ntc}}{R_0}\right)$$
   $$T_C = T - 273.15$$
   $$\text{temp\_c\_x100} = \text{(int16\_t)}\text{roundf}(T_C \times 100.0\text{f})$$

---

## 5. Required Code Modifications

### 1. `lib/FLC100_ADS131/FLC100_ADS131.h` & `.cpp`
- In `begin()`:
  Configure `ADS131_REG_CH4SET` with `0x10` (Gain 1, Normal input). Only power down channels 5 through 8 (`0x81`).
- In `readAndPushSample()`:
  Save the latest raw 24-bit reading of Channel 4 into a member variable `_lastRawCh4`.
- Add getter method:
  `int32_t getLatestChannel4Raw();` or `float readTemperatureC();`

### 2. `src/main.cpp`
- In `checkBleBurstTransmission()`:
  When preparing `SensorBatchPacket batch`:
  ```cpp
  if (sensor && sensorTypeConfig == 1) {
      float tC = static_cast<FLC100_ADS131*>(sensor)->readTemperatureC();
      batch.temp_c_x100 = (int16_t)roundf(tC * 100.0f);
  } else {
      batch.temp_c_x100 = 0x7FFF; // Sentinel: invalid/not supported
  }
  ```

### 3. `src/receiver/IngestionPipeline.cpp`
- In `ingestBatch()`:
  Replace hardcoded `item.temp = 0.0f;` with:
  ```cpp
  if (batch.temp_c_x100 != 0x7FFF) {
      item.temp = (float)batch.temp_c_x100 / 100.0f;
  } else {
      item.temp = 0.0f;
  }
  ```
- The existing CSV output formatter automatically outputs the real temperature in the 7th column:
  `CREEK,2161000,-8415.58,-27382.00,5845.43,004D4F,23.50,4.03,-52`

### 4. `src/receiver/BLEReceiver.cpp`
- Ensure `minBatchSize` calculation uses updated `sizeof(SensorBatchPacket)`.

---

## 6. Implementation & Verification Steps (When Ready)
1. **Confirm Schematic**: Check PCB schematic for thermistor divider orientation (high side vs low side) and resistor values ($R_b$, $R_0$, $B$).
2. **Apply Firmware Changes**: Update packet definitions, ADC driver, burst assembler, and receiver pipeline.
3. **Flash Both Devices**:
   - `esp32-c6-devkitc-1` on `/dev/ttyACM1` (CREEK)
   - `esp32c3_receiver` on `/dev/ttyACM0` (SUPERMINI_GATEWAY)
4. **Verification**:
   - Observe Gateway CSV stream: 7th column should reflect ambient temperature (e.g. `~22.0` to `~26.0` °C).
   - Place a warm finger on the PCB thermistor and verify temperature increases in real time.
