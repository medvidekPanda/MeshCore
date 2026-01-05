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
    #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
      Serial.println(ds3231_success ? "[RTC] DS3231 initialized successfully" : "[RTC] DS3231 initialization failed");
    #endif
    
    // Pokud je DS3231 úspěšně inicializován, načti čas z modulu
    #ifdef ESP_PLATFORM
    if (ds3231_success && should_sync_from_external_rtc) {
      // Načti čas z DS3231 modulu a nastav ho do systému
      DateTime rtc_time = rtc_3231.now();
      uint32_t rtc_unixtime = rtc_time.unixtime();
      #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
        Serial.print("[RTC] DS3231 current time: ");
        Serial.print(rtc_unixtime);
        Serial.print(" (");
        Serial.print(rtc_time.year());
        Serial.print("-");
        Serial.print(rtc_time.month());
        Serial.print("-");
        Serial.print(rtc_time.day());
        Serial.print(" ");
        Serial.print(rtc_time.hour());
        Serial.print(":");
        Serial.print(rtc_time.minute());
        Serial.print(":");
        Serial.print(rtc_time.second());
        Serial.println(")");
      #endif
      // Ověř, že čas je rozumný (větší než 1. ledna 2020 = 1577836800)
      if (rtc_unixtime > 1577836800) {
        // Nastav čas do systému přímo (před tím, než fallback obnoví čas z RTC paměti)
        _fallback->setCurrentTime(rtc_unixtime);
        #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
          Serial.println("[RTC] DS3231 time is valid - system time updated");
        #endif
      } else {
        // RTC má nevalidní čas - RTC neběží (baterie vybitá nebo čas nikdy nebyl nastaven)
        // Čas se nastaví až když přijde ADVERT packet (v setCurrentTime())
        #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
          Serial.println("[RTC] DS3231 time is INVALID (< 1.1.2020) - RTC not running, waiting for ADVERT sync");
        #endif
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
    #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
      Serial.println("[RTC] RV3028 initialized successfully");
    #endif
    
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
      #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
        Serial.print("[RTC] RV3028 current time: ");
        Serial.print(rtc_unixtime);
        Serial.print(" (");
        Serial.print(rtc_time.year());
        Serial.print("-");
        Serial.print(rtc_time.month());
        Serial.print("-");
        Serial.print(rtc_time.day());
        Serial.print(" ");
        Serial.print(rtc_time.hour());
        Serial.print(":");
        Serial.print(rtc_time.minute());
        Serial.print(":");
        Serial.print(rtc_time.second());
        Serial.println(")");
      #endif
      // Ověř, že čas je rozumný (větší než 1. ledna 2020 = 1577836800)
      if (rtc_unixtime > 1577836800) {
        _fallback->setCurrentTime(rtc_unixtime);
        #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
          Serial.println("[RTC] RV3028 time is valid - system time updated");
        #endif
      } else {
        #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
          Serial.println("[RTC] RV3028 time is INVALID (< 1.1.2020) - ignoring");
        #endif
      }
    }
    #endif
  }
  if(i2c_probe(wire,PCF8563_ADDRESS)){
    rtc_8563_success = rtc_8563.begin(&wire);
    #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
      Serial.println(rtc_8563_success ? "[RTC] PCF8563 initialized successfully" : "[RTC] PCF8563 initialization failed");
    #endif
    
    // Stejně pro PCF8563 (pouze pokud DS3231 a RV3028 nejsou dostupné)
    #ifdef ESP_PLATFORM
    if (rtc_8563_success && should_sync_from_external_rtc && !ds3231_success && !rv3028_success) {
      // Načti čas z PCF8563 modulu a nastav ho do systému
      DateTime rtc_time = rtc_8563.now();
      uint32_t rtc_unixtime = rtc_time.unixtime();
      #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
        Serial.print("[RTC] PCF8563 current time: ");
        Serial.print(rtc_unixtime);
        Serial.print(" (");
        Serial.print(rtc_time.year());
        Serial.print("-");
        Serial.print(rtc_time.month());
        Serial.print("-");
        Serial.print(rtc_time.day());
        Serial.print(" ");
        Serial.print(rtc_time.hour());
        Serial.print(":");
        Serial.print(rtc_time.minute());
        Serial.print(":");
        Serial.print(rtc_time.second());
        Serial.println(")");
      #endif
      // Ověř, že čas je rozumný (větší než 1. ledna 2020 = 1577836800)
      if (rtc_unixtime > 1577836800) {
        _fallback->setCurrentTime(rtc_unixtime);
        #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
          Serial.println("[RTC] PCF8563 time is valid - system time updated");
        #endif
      } else {
        #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
          Serial.println("[RTC] PCF8563 time is INVALID (< 1.1.2020) - ignoring");
        #endif
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

bool AutoDiscoverRTCClock::isRTCRunning() const {
  // Zkontroluj, jestli RTC modul má validní čas (> 1.1.2020)
  uint32_t rtc_time = 0;
  if (ds3231_success) {
    rtc_time = rtc_3231.now().unixtime();
  } else if (rv3028_success) {
    rtc_time = DateTime(
        rtc_rv3028.getYear(),
        rtc_rv3028.getMonth(),
        rtc_rv3028.getDate(),
        rtc_rv3028.getHour(),
        rtc_rv3028.getMinute(),
        rtc_rv3028.getSecond()
    ).unixtime();
  } else if (rtc_8563_success) {
    rtc_time = rtc_8563.now().unixtime();
  } else {
    return false;  // Žádný RTC modul není dostupný
  }
  
  // Validní čas je větší než 1. ledna 2020 (1577836800)
  return (rtc_time > 1577836800);
}

const char* AutoDiscoverRTCClock::getRTCStatus() const {
  // Zkontroluj, jestli je RTC modul detekován
  if (!ds3231_success && !rv3028_success && !rtc_8563_success) {
    return "NOT_DETECTED";  // RTC modul není detekován na I2C sběrnici
  }
  
  // Zkontroluj, jestli má validní čas
  uint32_t rtc_time = 0;
  const char* rtc_type = "UNKNOWN";
  
  if (ds3231_success) {
    rtc_time = rtc_3231.now().unixtime();
    rtc_type = "DS3231";
  } else if (rv3028_success) {
    rtc_time = DateTime(
        rtc_rv3028.getYear(),
        rtc_rv3028.getMonth(),
        rtc_rv3028.getDate(),
        rtc_rv3028.getHour(),
        rtc_rv3028.getMinute(),
        rtc_rv3028.getSecond()
    ).unixtime();
    rtc_type = "RV3028";
  } else if (rtc_8563_success) {
    rtc_time = rtc_8563.now().unixtime();
    rtc_type = "PCF8563";
  }
  
  if (rtc_time > 1577836800) {
    return "OK";  // RTC má validní čas
  } else {
    return "INVALID_TIME";  // RTC má nevalidní čas (pravděpodobně baterie vybitá nebo čas nebyl nastaven)
  }
}

void AutoDiscoverRTCClock::setCurrentTime(uint32_t time) {
  #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
    Serial.print("[RTC] setCurrentTime called with: ");
    Serial.println(time);
  #endif
  
  if (ds3231_success) {
    rtc_3231.adjust(DateTime(time));
    delay(50);  // Počkej na dokončení zápisu do DS3231
    // Ověř, že se čas skutečně zapsal
    DateTime verify_time = rtc_3231.now();
    uint32_t verify_unixtime = verify_time.unixtime();
    #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
      Serial.print("[RTC] DS3231 write: ");
      Serial.print(time);
      Serial.print(" -> read back: ");
      Serial.print(verify_unixtime);
      if (verify_unixtime == time || abs((int32_t)(verify_unixtime - time)) <= 1) {
        Serial.println(" OK");
      } else {
        Serial.print(" MISMATCH (diff: ");
        Serial.print((int32_t)(verify_unixtime - time));
        Serial.println(")");
      }
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
