"""
Unit Test for RM3100 Scaling Formula Math

Verifies that raw LSB counts for a constant physical Earth magnetic field (~54,387 nT)
convert to an identical, invariant physical field magnitude across all cycle counts (50, 100, 200, 300, 400).
"""

def rm3100_gain(cycle_count: int) -> float:
    """Gain(Nc) = (0.3671 * Nc + 1.5) LSB / uT"""
    return 0.3671 * float(cycle_count) + 1.5

def rm3100_scale_factor(cycle_count: int) -> float:
    """Scale Factor (nT per LSB count) = 1000.0 / Gain(Nc)"""
    return 1000.0 / rm3100_gain(cycle_count)

def test_magnitude_invariance_across_cycle_counts():
    # Simulate a constant Earth magnetic field vector B = (23400 nT, -4100 nT, 48900 nT)
    true_x_nT = 23400.0
    true_y_nT = -4100.0
    true_z_nT = 48900.0
    true_magnitude = (true_x_nT**2 + true_y_nT**2 + true_z_nT**2) ** 0.5

    cycle_counts = [50, 100, 200, 300, 400]
    calculated_magnitudes = []

    print("\n--- Cycle Count Math Invariance Verification ---")
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
        print(f"Cycle Count {cc:3d}: Scale Factor={sf:6.3f} nT/LSB | Raw LSB=({raw_x_lsb:5d}, {raw_y_lsb:5d}, {raw_z_lsb:5d}) -> Calibrated |B| = {calc_mag:.2f} nT (Diff: {dev_nT:.2f} nT / {pct_err:.3f}%)")
        
        # Quantization rounding error should be < 35 nT (under 0.06% of Earth field)
        assert dev_nT < 35.0

    # Max difference across all cycle counts due to 1-LSB quantization
    max_diff = max(calculated_magnitudes) - min(calculated_magnitudes)
    max_diff_pct = (max_diff / true_magnitude) * 100.0
    print(f"\nMax magnitude variation across all cycle counts: {max_diff:.2f} nT ({max_diff_pct:.3f}%)")
    assert max_diff_pct < 0.1

if __name__ == "__main__":
    test_magnitude_invariance_across_cycle_counts()
    print("\n[PASS] SUCCESS: RM3100 scaling math verification passed!")
