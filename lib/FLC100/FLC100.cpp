#include "FLC100.h"

FLC100::FLC100(int pinX, int pinY, int pinZ) {
    _pins[0] = pinX;
    _pins[1] = pinY;
    _pins[2] = pinZ;
}

bool FLC100::begin() {
    for (int i = 0; i < 3; i++) {
        pinMode(_pins[i], INPUT);
    }
    // Analog sensors are always "ready" after pin setup
    return true;
}

bool FLC100::dataReady() {
    // For analog sensors, we can always read. 
    // In a more advanced version, we could use a timer to enforce a sample rate.
    return true;
}

void FLC100::readXYZ(int32_t &x, int32_t &y, int32_t &z) {
    int32_t vals[3];
    for (int i = 0; i < 3; i++) {
        // Read ADC
        uint16_t raw = analogRead(_pins[i]);
        
        // Convert to mV at the ESP32 pin
        float pin_mv = (raw * _adc_vref_mv) / _adc_max_counts;
        
        // Account for voltage divider to get original sensor voltage
        float sensor_mv = pin_mv / _divider_ratio;
        
        // Convert to field value (nT)
        // (sensor_mv - offset_mv) / (sensitivity_uv_ut / 1000) * 1000 nT/uT
        float field_ut = (sensor_mv - _offset_mv) / (_sensitivity_uv_ut / 1000.0f);
        vals[i] = (int32_t)(field_ut * 1000.0f); // Convert to nanoTesla
    }
    x = vals[0];
    y = vals[1];
    z = vals[2];
}

void FLC100::setContinuousMode(bool enable, uint8_t rate_code) {
    // Analog sensors are effectively always in continuous mode.
    // rate_code is ignored for now.
}

String FLC100::getStatusString() {
    String s = "Analog Config: Pins(";
    s += String(_pins[0]) + "," + String(_pins[1]) + "," + String(_pins[2]) + ")";
    s += " | Offset: " + String(_offset_mv) + "mV";
    return s;
}

void FLC100::setCalibration(float offset_mv, float sensitivity_uv_ut, float divider_ratio) {
    _offset_mv = offset_mv;
    _sensitivity_uv_ut = sensitivity_uv_ut;
    _divider_ratio = divider_ratio;
}
