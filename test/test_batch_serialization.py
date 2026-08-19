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
        #     CompactSample samples[10];          // 120 bytes
        # } SensorBatchPacket;                  // Total = 139 bytes
        self.compact_sample_fmt = "<iii" # 3 x int32_t (x, y, z in nT) = 12 bytes
        self.batch_hdr_fmt = "<8sIHBHH"  # 8s + uint32 + uint16 + uint8 + uint16 + uint16 = 19 bytes header

    def test_compact_sample_packing(self):
        """Verify 3-axis sample packing into 12-byte compact struct."""
        x_nT, y_nT, z_nT = 21550, -3240, 43180
        packed = struct.pack(self.compact_sample_fmt, x_nT, y_nT, z_nT)
        self.assertEqual(len(packed), 12)
        
        unpacked_x, unpacked_y, unpacked_z = struct.unpack(self.compact_sample_fmt, packed)
        self.assertEqual(unpacked_x, x_nT)
        self.assertEqual(unpacked_y, y_nT)
        self.assertEqual(unpacked_z, z_nT)

    def test_10_sample_batch_reconstruction(self):
        """Simulate packing 10 samples and verifying timestamp reconstruction using packet age."""
        device_id = b"NODE_3A\x00"
        latest_sample_age_ms = 250 # Sample was measured 250ms before radio burst (e.g. 2nd retry)
        interval_ms = 1000         # 1 second spacing
        sample_count = 10
        status = 0x4D4F
        vbat_mv = 3300

        hdr_bytes = struct.pack(self.batch_hdr_fmt, device_id, latest_sample_age_ms, interval_ms, sample_count, status, vbat_mv)
        self.assertEqual(len(hdr_bytes), 19)

        samples_bytes = bytearray()
        expected_samples = []
        for i in range(sample_count):
            x = 20000 + i * 10
            y = -3000 - i * 5
            z = 43000 + i * 20
            expected_samples.append((x, y, z))
            samples_bytes.extend(struct.pack(self.compact_sample_fmt, x, y, z))

        full_payload = hdr_bytes + samples_bytes
        self.assertEqual(len(full_payload), 19 + 120) # 139 bytes

        # Unpack header
        dev_id_out, age_out, interval_out, count_out, status_out, vbat_out = struct.unpack(self.batch_hdr_fmt, full_payload[:19])
        self.assertEqual(dev_id_out.decode('utf-8').rstrip('\x00'), "NODE_3A")
        self.assertEqual(age_out, latest_sample_age_ms)
        self.assertEqual(interval_out, 1000)
        self.assertEqual(count_out, 10)
        self.assertEqual(vbat_out, 3300)

        # Simulate receiver local time
        rx_arrival_ms = 50000
        latest_sample_ts_ms = rx_arrival_ms - age_out  # 49750 ms

        # Unpack samples and reconstruct timestamps
        for i in range(count_out):
            offset = 19 + (i * 12)
            x, y, z = struct.unpack(self.compact_sample_fmt, full_payload[offset:offset+12])
            offset_from_newest = (count_out - 1 - i) * interval_out
            sample_ts_ms = latest_sample_ts_ms - offset_from_newest
            
            self.assertEqual((x, y, z), expected_samples[i])
            self.assertEqual(sample_ts_ms, 49750 - (9 - i) * 1000)

if __name__ == "__main__":
    unittest.main()
