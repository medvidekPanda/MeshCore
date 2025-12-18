#include "AutoDiscoverRTCClock.h"
#include "RTClib.h"
#include <Melopero_RV3028.h>

#ifdef ESP_PLATFORM
#include <esp_system.h>  // for esp_reset_reason()
#endif

static RTC_DS3231 rtc_3231;
static bool ds3231_success = false;

static Melopero_RV3028 rtc_rv3028;
static bool rv3028_success = false;

static RTC_PCF8563 rtc_8563;
static bool rtc_8563_success = false;

#define DS3231_ADDRESS   0x68
#define RV3028_ADDRESS   0x52
#define PCF8563_ADDRESS  0x51

bool AutoDiscoverRTCClock::i2c_probe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

void AutoDiscoverRTCClock::begin(TwoWire& wire) {
  // Při startu nebo probuzení z deep sleep načti čas z externího RTC modulu PRVNÍ
  // (před tím, než fallback ESP32RTCClock obnoví čas z RTC paměti ESP32)
  #ifdef ESP_PLATFORM
  bool should_sync_from_external_rtc = false;
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP || reason == ESP_RST_POWERON) {
    should_sync_from_external_rtc = true;
  }
  #endif

  if (i2c_probe(wire, DS3231_ADDRESS)) {
    ds3231_success = rtc_3231.begin(&wire);
    
    // Pokud je DS3231 úspěšně inicializován, načti čas z modulu
    #ifdef ESP_PLATFORM
    if (ds3231_success && should_sync_from_external_rtc) {
      // Načti čas z DS3231 modulu a nastav ho do systému
      DateTime rtc_time = rtc_3231.now();
      uint32_t rtc_unixtime = rtc_time.unixtime();
      // Ověř, že čas je rozumný (větší než 1. ledna 2020 = 1577836800)
      if (rtc_unixtime > 1577836800) {
        // Nastav čas do systému přímo (před tím, než fallback obnoví čas z RTC paměti)
        _fallback->setCurrentTime(rtc_unixtime);
      }
    }
    #endif
  }
  if (i2c_probe(wire, RV3028_ADDRESS)) {
    rtc_rv3028.initI2C(wire);
	rtc_rv3028.writeToRegister(0x35, 0x00);
	rtc_rv3028.writeToRegister(0x37, 0xB4); // Direct Switching Mode (DSM): when VDD < VBACKUP, switchover occurs from VDD to VBACKUP
	rtc_rv3028.set24HourMode(); // Set the device to use the 24hour format (default) instead of the 12 hour format
    rv3028_success = true;
    
    // Stejně pro RV3028 (pouze pokud DS3231 není dostupný)
    #ifdef ESP_PLATFORM
    if (rv3028_success && should_sync_from_external_rtc && !ds3231_success) {
      // Načti čas z RV3028 modulu a nastav ho do systému
      DateTime rtc_time = DateTime(
        rtc_rv3028.getYear(),
        rtc_rv3028.getMonth(),
        rtc_rv3028.getDate(),
        rtc_rv3028.getHour(),
        rtc_rv3028.getMinute(),
        rtc_rv3028.getSecond()
      );
      uint32_t rtc_unixtime = rtc_time.unixtime();
      // Ověř, že čas je rozumný (větší než 1. ledna 2020 = 1577836800)
      if (rtc_unixtime > 1577836800) {
        _fallback->setCurrentTime(rtc_unixtime);
      }
    }
    #endif
  }
  if(i2c_probe(wire,PCF8563_ADDRESS)){
    rtc_8563_success = rtc_8563.begin(&wire);
    
    // Stejně pro PCF8563 (pouze pokud DS3231 a RV3028 nejsou dostupné)
    #ifdef ESP_PLATFORM
    if (rtc_8563_success && should_sync_from_external_rtc && !ds3231_success && !rv3028_success) {
      // Načti čas z PCF8563 modulu a nastav ho do systému
      DateTime rtc_time = rtc_8563.now();
      uint32_t rtc_unixtime = rtc_time.unixtime();
      // Ověř, že čas je rozumný (větší než 1. ledna 2020 = 1577836800)
      if (rtc_unixtime > 1577836800) {
        _fallback->setCurrentTime(rtc_unixtime);
      }
    }
    #endif
  }
}

uint32_t AutoDiscoverRTCClock::getCurrentTime() {
  if (ds3231_success) {
    return rtc_3231.now().unixtime();
  }
  if (rv3028_success) {
    return DateTime(
        rtc_rv3028.getYear(),
        rtc_rv3028.getMonth(),
        rtc_rv3028.getDate(),
        rtc_rv3028.getHour(),
        rtc_rv3028.getMinute(),
        rtc_rv3028.getSecond()
    ).unixtime();
  }
  if(rtc_8563_success){
    return rtc_8563.now().unixtime();
  }
  return _fallback->getCurrentTime();
}

void AutoDiscoverRTCClock::setCurrentTime(uint32_t time) {
  #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
    Serial.print("[RTC] setCurrentTime called with: ");
    Serial.println(time);
  #endif
  
  if (ds3231_success) {
    rtc_3231.adjust(DateTime(time));
    #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
      Serial.println("[RTC] DS3231 external RTC updated");
    #endif
  } else if (rv3028_success) {
    auto dt = DateTime(time);
	  uint8_t weekday = (dt.day() + (uint16_t)((2.6 * dt.month()) - 0.2) - (2 * (dt.year() / 100)) + dt.year() + (uint16_t)(dt.year() / 4) + (uint16_t)(dt.year() / 400)) % 7;
    rtc_rv3028.setTime(dt.year(), dt.month(), weekday, dt.day(), dt.hour(), dt.minute(), dt.second());
    #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
      Serial.println("[RTC] RV3028 external RTC updated");
    #endif
  } else if (rtc_8563_success) {
    rtc_8563.adjust(DateTime(time));
    #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
      Serial.println("[RTC] PCF8563 external RTC updated");
    #endif
  }
  
  // Vždy aktualizuj i fallback (systémový čas a RTC paměť ESP32)
  _fallback->setCurrentTime(time);
  #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
    Serial.println("[RTC] ESP32 system time and RTC memory updated");
  #endif
}
