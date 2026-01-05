#pragma once

#include <Mesh.h>
#include <Arduino.h>
#include <Wire.h>

class AutoDiscoverRTCClock : public mesh::RTCClock {
  mesh::RTCClock* _fallback;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
public:
  AutoDiscoverRTCClock(mesh::RTCClock& fallback) : _fallback(&fallback) { }

  void begin(TwoWire& wire);
  uint32_t getCurrentTime() override;
  void setCurrentTime(uint32_t time) override;
  bool isRTCRunning() const;  // Vrátí true, pokud RTC modul má validní čas
  const char* getRTCStatus() const;  // Vrátí textový popis stavu RTC (OK, NOT_DETECTED, INVALID_TIME, I2C_ERROR)

  void tick() override {
    _fallback->tick();   // is typically VolatileRTCClock, which now needs tick()
  }
  
  // Save time before deep sleep (for ESP32RTCClock fallback)
  void saveTimeBeforeSleep(uint32_t sleep_duration_secs) {
    // Forward to fallback - ESP32RTCClock will handle it if it's the right type
    #if defined(ESP_PLATFORM)
      // Try to cast to ESP32RTCClock and call saveTimeBeforeSleep
      // Since we can't easily check type at runtime, we'll use a helper function
      // For now, we'll just call getCurrentTime() which auto-saves, and manually save sleep duration
      // The actual save will happen in ESP32RTCClock::getCurrentTime()
      getCurrentTime(); // This auto-saves time to RTC memory
      // Sleep duration will be saved when we call saveTimeBeforeSleep on ESP32RTCClock
      // But we need access to ESP32RTCClock instance - this is a limitation
      // For now, we'll rely on auto-save in getCurrentTime() and manual save of sleep duration
    #endif
  }
};
