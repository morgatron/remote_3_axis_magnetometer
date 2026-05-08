#ifndef FLC100_H
#define FLC100_H

#include "Magnetometer.h"

/**
 * @brief Implementation for a 3-axis magnetometer using three Stefan Mayer Instruments FLC-100 fluxgates.
 * 
 * NOTE: FLC-100 outputs 0.5V to 4.5V (centered at 2.5V). 
 * ESP32 ADC pins are NOT 5V tolerant. Use a voltage divider (e.g., 10k / 20k) 
 * to scale the output down to the 0-3.3V range.
 */
class FLC100 : public Magnetometer {
public:
    /**
     * @param pinX ADC pin for X-axis
     * @param pinY ADC pin for Y-axis
     * @param pinZ ADC pin for Z-axis
     */
    FLC100(int pinX, int pinY, int pinZ);

    // Magnetometer interface
    bool begin() override;
    bool dataReady() override;
    void readXYZ(int32_t &x, int32_t &y, int32_t &z) override;
    void setContinuousMode(bool enable, uint8_t rate_code = 0) override;
    String getStatusString() override;
    String getSensorName() override { return "FLC100-3Axis"; }

    /**
     * @brief Set calibration parameters for the analog-to-field conversion.
     * @param offset_mv The voltage in mV corresponding to 0uT (typically 2500mV).
     * @param sensitivity_uv_ut Sensitivity in microvolts per microTesla (typically 20000uV/uT).
     * @param divider_ratio The ratio of the external voltage divider (V_out / V_in).
     */
    void setCalibration(float offset_mv, float sensitivity_uv_ut, float divider_ratio);

private:
    int _pins[3];
    float _offset_mv = 2500.0f;
    float _sensitivity_uv_ut = 20000.0f;
    float _divider_ratio = 0.666f; // Default for 10k/20k divider (20/(10+20))
    
    // ESP32 ADC characteristics
    const float _adc_vref_mv = 3300.0f;
    const float _adc_max_counts = 4095.0f;
};

#endif
