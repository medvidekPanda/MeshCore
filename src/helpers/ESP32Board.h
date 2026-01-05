#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#if defined(ESP_PLATFORM)

#include <rom/rtc.h>
#include <sys/time.h>
#include <Wire.h>
#include "esp_wifi.h"
#include "driver/rtc_io.h"

class ESP32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin() {
    // for future use, sub-classes SHOULD call this from their begin()
    startup_reason = BD_STARTUP_NORMAL;

  #ifdef ESP32_CPU_FREQ
    setCpuFrequencyMhz(ESP32_CPU_FREQ);
  #endif

  #ifdef PIN_VBAT_READ
    // battery read support
    pinMode(PIN_VBAT_READ, INPUT);
    adcAttachPin(PIN_VBAT_READ);
  #endif

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
   #if PIN_BOARD_SDA >= 0 && PIN_BOARD_SCL >= 0
    Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);
   #endif
  #else
    Wire.begin();
  #endif
  }

  // Temperature from ESP32 MCU
  float getMCUTemperature() override {
    uint32_t raw = 0;

    // To get and average the temperature so it is more accurate, especially in low temperature
    for (int i = 0; i < 4; i++) {
      raw += temperatureRead();
    }

    return raw / 4;
  }

  void enterLightSleep(uint32_t secs) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(P_LORA_DIO_1) // Supported ESP32 variants
    if (rtc_gpio_is_valid_gpio((gpio_num_t)P_LORA_DIO_1)) { // Only enter sleep mode if P_LORA_DIO_1 is RTC pin
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
      esp_sleep_enable_ext1_wakeup((1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH); // To wake up when receiving a LoRa packet

      if (secs > 0) {
        esp_sleep_enable_timer_wakeup(secs * 1000000); // To wake up every hour to do periodically jobs
      }

      esp_light_sleep_start(); // CPU enters light sleep
    }
#endif
  }

  void sleep(uint32_t secs) override {
    // To check for WiFi status to see if there is active OTA
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    
    if (err != ESP_OK) {          // WiFi is off ~ No active OTA, safe to go to sleep
      enterLightSleep(secs);      // To wake up after "secs" seconds or when receiving a LoRa packet
    }
  }

  uint8_t getStartupReason() const override { return startup_reason; }
  void setStartupReason(uint8_t reason) { startup_reason = reason; }
  void clearStartupReason() { startup_reason = BD_STARTUP_NORMAL; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#elif defined(P_LORA_TX_NEOPIXEL_LED)
  #define NEOPIXEL_BRIGHTNESS    64  // white brightness (max 255)

  void onBeforeTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS);   // turn TX neopixel on (White)
  }
  void onAfterTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, 0, 0, 0);   // turn TX neopixel off
  }
#endif

  uint16_t getBattMilliVolts() override {
  #ifdef PIN_VBAT_READ
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
    }
    raw = raw / 4;

    return (2 * raw);
  #else
    return 0;  // not supported
  #endif
  }

  const char* getManufacturerName() const override {
    return "Generic ESP32";
  }

  void reboot() override {
    esp_restart();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
};

// RTC memory structure to persist time across deep sleep
// RTC memory survives deep sleep but not power-on reset
RTC_DATA_ATTR static struct {
  uint32_t saved_time;      // Time saved before deep sleep
  uint32_t sleep_duration;  // Sleep duration in seconds (for timer wakeup)
  uint32_t magic;            // Magic number to validate data (0xDEADBEEF)
} rtc_time_data = {0, 0, 0};

class ESP32RTCClock : public mesh::RTCClock {
public:
  ESP32RTCClock() { }
  void begin() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON) {
      // First power-on: start with some date/time in the recent past
      struct timeval tv;
      tv.tv_sec = 1715770351;  // 15 May 2024, 8:50pm
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      // Clear RTC memory on power-on
      rtc_time_data.saved_time = 0;
      rtc_time_data.sleep_duration = 0;
      rtc_time_data.magic = 0;
    } else if (reason == ESP_RST_DEEPSLEEP) {
      // After deep sleep wakeup: restore time from RTC memory
      // ESP32 system time may reset or drift, so we restore from saved time + sleep duration
      // POZNÁMKA: Pokud byl čas už nastaven externím RTC modulem (DS3231 atd.) před voláním begin(),
      // pak current_time bude větší než saved_time a čas z RTC paměti se nepoužije
      if (rtc_time_data.magic == 0xDEADBEEF && rtc_time_data.saved_time > 0) {
        time_t current_time;
        time(&current_time);
        
        // If current time is less than saved time, it means system time was reset
        // AND externí RTC modul ještě nenastavil čas (protože by jinak current_time >= saved_time)
        // Restore from saved time + sleep duration
        if (current_time < rtc_time_data.saved_time) {
          // System time was reset - restore from saved time + sleep duration
          uint32_t restored_time = rtc_time_data.saved_time + rtc_time_data.sleep_duration;
          
          struct timeval tv;
          tv.tv_sec = restored_time;
          tv.tv_usec = 0;
          settimeofday(&tv, NULL);
        }
        // If current time >= saved time, system time continued correctly OR
        // externí RTC modul už nastavil čas, no action needed
      }
    }
  }
  
  // Call this before entering deep sleep to save current time
  void saveTimeBeforeSleep(uint32_t sleep_duration_secs) {
    time_t now;
    time(&now);
    rtc_time_data.saved_time = now;
    rtc_time_data.sleep_duration = sleep_duration_secs;
    rtc_time_data.magic = 0xDEADBEEF;
  }
  
  uint32_t getCurrentTime() override {
    time_t _now;
    time(&_now);
    // Auto-save time to RTC memory periodically (every 10 seconds) to keep it updated
    static uint32_t last_save = 0;
    if (_now > 0 && (_now - last_save > 10 || last_save == 0)) {
      rtc_time_data.saved_time = _now;
      rtc_time_data.magic = 0xDEADBEEF;
      last_save = _now;
    }
    return _now;
  }
  void setCurrentTime(uint32_t time) override { 
    struct timeval tv;
    tv.tv_sec = time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    // Also update saved time when time is set externally
    rtc_time_data.saved_time = time;
    rtc_time_data.magic = 0xDEADBEEF;
  }
};

#endif
