"""
Unit Test for RM3100 Scaling Formula Math

Verifies that raw LSB counts for a constant physical Earth magnetic field (~54,387 nT)
convert to an identical, invariant physical field magnitude across all cycle counts (50, 100, 200, 300, 400).
"""

import unittest

def rm3100_gain(cycle_count: int) -> float:
    """Gain(Nc) = (0.3671 * Nc + 1.5) LSB / uT"""
    return 0.3671 * float(cycle_count) + 1.5

def rm3100_scale_factor(cycle_count: int) -> float:
    """Scale Factor (nT per LSB count) = 1000.0 / Gain(Nc)"""
    return 1000.0 / rm3100_gain(cycle_count)

class TestScalingMath(unittest.TestCase):
    def test_magnitude_invariance_across_cycle_counts(self):
        # Simulate a constant Earth magnetic field vector B = (23400 nT, -4100 nT, 48900 nT)
        true_x_nT = 23400.0
        true_y_nT = -4100.0
        true_z_nT = 48900.0
        true_magnitude = (true_x_nT**2 + true_y_nT**2 + true_z_nT**2) ** 0.5

        cycle_counts = [50, 100, 200, 300, 400]
        calculated_magnitudes = []

        for cc in cycle_counts:
            sf = rm3100_scale_factor(cc)
            
            # Simulate what the raw RM3100 sensor 24-bit LSB registers output at cycle count cc
            raw_x_lsb = int(round(true_x_nT / sf))
            raw_y_lsb = int(round(true_y_nT / sf))
            raw_z_lsb = int(round(true_z_nT / sf))

            # Convert back using MCU scale factor
            calc_x_nT = float(raw_x_lsb) * sf
            calc_y_nT = float(raw_y_lsb) * sf
            calc_z_nT = float(raw_z_lsb) * sf
            calc_mag = (calc_x_nT**2 + calc_y_nT**2 + calc_z_nT**2) ** 0.5
            
            calculated_magnitudes.append(calc_mag)

            # Deviation from true magnitude
            dev_nT = abs(calc_mag - true_magnitude)
            pct_err = (dev_nT / true_magnitude) * 100.0
            
            # Quantization rounding error should be < 35 nT (under 0.06% of Earth field)
            self.assertLess(dev_nT, 35.0, f"Cycle count {cc} error exceeded 35 nT: {dev_nT} nT")

        # Max difference across all cycle counts due to 1-LSB quantization
        max_diff = max(calculated_magnitudes) - min(calculated_magnitudes)
        max_diff_pct = (max_diff / true_magnitude) * 100.0
        self.assertLess(max_diff_pct, 0.1, f"Max difference pct exceeded 0.1%: {max_diff_pct}%")

if __name__ == "__main__":
    unittest.main()

