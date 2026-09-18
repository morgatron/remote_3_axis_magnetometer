"""
Host Unit Test for LE Coded PHY SensorBatchPacket Serialization (`test_batch_serialization.py`)

Verifies C-struct memory alignment, endianness, compact sample packing,
and microsecond timestamp reconstruction logic without requiring physical hardware.
"""

import os
import sys
import struct
import unittest

class TestBatchSerialization(unittest.TestCase):

    def setUp(self):
        # C struct layout matching TelemetryPacket.h __attribute__((packed)):
        # typedef struct __attribute__((packed)) {
        #     char          device_id[8];         // 8 bytes
        #     uint32_t      latest_sample_age_ms; // 4 bytes (age of newest sample in batch at TX time)
        #     uint16_t      sample_interval_ms;   // 2 bytes
        #     uint8_t       sample_count;         // 1 byte
        #     uint16_t      status;               // 2 bytes
        #     uint16_t      vbat_mv;              // 2 bytes
        #     int16_t       temp_c_x100;          // 2 bytes
        #     CompactSample samples[10];          // 120 bytes
        # } SensorBatchPacket;                  // Total = 141 bytes
        self.compact_sample_fmt = "<fff" # 3 x float (x, y, z in nT) = 12 bytes
        self.batch_hdr_fmt = "<8sIHBHHh"  # 8s + uint32 + uint16 + uint8 + uint16 + uint16 + int16 = 21 bytes header

    def test_compact_sample_packing(self):
        """Verify 3-axis sample packing into 12-byte compact struct."""
        x_nT, y_nT, z_nT = 21550.75, -3240.25, 43180.50
        packed = struct.pack(self.compact_sample_fmt, x_nT, y_nT, z_nT)
        self.assertEqual(len(packed), 12)
        
        unpacked_x, unpacked_y, unpacked_z = struct.unpack(self.compact_sample_fmt, packed)
        self.assertAlmostEqual(unpacked_x, x_nT, places=2)
        self.assertAlmostEqual(unpacked_y, y_nT, places=2)
        self.assertAlmostEqual(unpacked_z, z_nT, places=2)

    def test_10_sample_batch_reconstruction(self):
        """Simulate packing 10 samples and verifying timestamp reconstruction using packet age."""
        device_id = b"NODE_3A\x00"
        latest_sample_age_ms = 250 # Sample was measured 250ms before radio burst (e.g. 2nd retry)
        interval_ms = 1000         # 1 second spacing
        sample_count = 10
        status = 0x4D4F
        vbat_mv = 3300
        temp_c_x100 = 2350         # 23.50 deg C

        hdr_bytes = struct.pack(self.batch_hdr_fmt, device_id, latest_sample_age_ms, interval_ms, sample_count, status, vbat_mv, temp_c_x100)
        self.assertEqual(len(hdr_bytes), 21)

        samples_bytes = bytearray()
        expected_samples = []
        for i in range(sample_count):
            x = 20000.12 + i * 10.5
            y = -3000.45 - i * 5.25
            z = 43000.80 + i * 20.1
            expected_samples.append((x, y, z))
            samples_bytes.extend(struct.pack(self.compact_sample_fmt, x, y, z))

        full_payload = hdr_bytes + samples_bytes
        self.assertEqual(len(full_payload), 21 + 120) # 141 bytes

        # Unpack header
        dev_id_out, age_out, interval_out, count_out, status_out, vbat_out, temp_out = struct.unpack(self.batch_hdr_fmt, full_payload[:21])
        self.assertEqual(dev_id_out.decode('utf-8').rstrip('\x00'), "NODE_3A")
        self.assertEqual(age_out, latest_sample_age_ms)
        self.assertEqual(interval_out, 1000)
        self.assertEqual(count_out, 10)
        self.assertEqual(vbat_out, 3300)
        self.assertEqual(temp_out, 2350)

        # Simulate receiver local time
        rx_arrival_ms = 50000
        latest_sample_ts_ms = rx_arrival_ms - age_out  # 49750 ms

        # Unpack samples and reconstruct timestamps
        for i in range(count_out):
            offset = 21 + (i * 12)
            x, y, z = struct.unpack(self.compact_sample_fmt, full_payload[offset:offset+12])
            offset_from_newest = (count_out - 1 - i) * interval_out
            sample_ts_ms = latest_sample_ts_ms - offset_from_newest
            
            self.assertAlmostEqual(x, expected_samples[i][0], places=2)
            self.assertAlmostEqual(y, expected_samples[i][1], places=2)
            self.assertAlmostEqual(z, expected_samples[i][2], places=2)
            self.assertEqual(sample_ts_ms, 49750 - (9 - i) * 1000)

    def test_gateway_adv_packet_serialization(self):
        """Verify GatewayAdvPacket C-struct memory layout, 31-byte header, and sample indexing."""
        # TelemetryPacket.h GatewayAdvPacket format:
        # uint16_t company_id (2)
        # uint8_t  magic[2] (2)
        # uint8_t  packet_seq (1)
        # char     node_id[8] (8)
        # uint64_t timestamp_us (8)
        # uint16_t sample_interval_ms (2)
        # uint8_t  sample_count (1)
        # uint16_t status (2)
        # uint16_t vbat_mv (2)
        # int16_t  temp_c_x100 (2)
        # int8_t   rssi (1)
        # uint16_t gw_vbat_mv (2)
        # Total header = 33 bytes (31 bytes from magic)
        gw_hdr_fmt = "<H2sB8sQHBHHhbH"
        self.assertEqual(struct.calcsize(gw_hdr_fmt), 33)

        company_id = 0xFFFF
        magic = b"MG"
        seq = 42
        node_id = b"BENCH001"
        ts_us = 1710000000000000
        interval_ms = 1000
        sample_count = 3
        status = 0x0002
        vbat_mv = 3750
        temp_c_x100 = 2450 # 24.50 C
        rssi = -64
        gw_vbat_mv = 4120  # 4.12 V

        hdr = struct.pack(gw_hdr_fmt, company_id, magic, seq, node_id, ts_us,
                          interval_ms, sample_count, status, vbat_mv, temp_c_x100, rssi, gw_vbat_mv)
        self.assertEqual(len(hdr), 33)

        samples = []
        samples_bytes = bytearray()
        for i in range(sample_count):
            x = 1234.5 + i
            y = -5678.9 - i
            z = 9012.3 + i
            samples.append((x, y, z))
            samples_bytes.extend(struct.pack(self.compact_sample_fmt, x, y, z))

        full_pkt = hdr + samples_bytes

        # Strip company ID to simulate raw manufacturer payload starting at magic
        raw_from_magic = full_pkt[2:]
        self.assertEqual(raw_from_magic[:2], b"MG")

        # Verify header offset and sample parsing
        header_len = 31
        for i in range(sample_count):
            s_offset = header_len + i * 12
            x, y, z = struct.unpack_from(self.compact_sample_fmt, raw_from_magic, s_offset)
            self.assertAlmostEqual(x, samples[i][0], places=2)
            self.assertAlmostEqual(y, samples[i][1], places=2)
            self.assertAlmostEqual(z, samples[i][2], places=2)

if __name__ == "__main__":
    unittest.main()
