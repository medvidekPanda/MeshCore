#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#ifdef ESP_PLATFORM
  #include <WiFi.h>
  #include <esp_bt.h>
  #include <esp_task_wdt.h>
#endif

class XiaoS3WIOBoard : public ESP32Board {
public:
  XiaoS3WIOBoard() { }

  void begin() {
    ESP32Board::begin();

    #if defined(A0) || defined(PIN_VBAT_READ)
      // Initialize battery voltage measurement pin (A0 on ESP32-S3 XIAO)
      // Use A0 constant if available (as per Seeed Studio wiki), otherwise use PIN_VBAT_READ
      int batt_pin = -1;
      #if defined(A0)
        batt_pin = A0;
      #elif defined(PIN_VBAT_READ)
        batt_pin = PIN_VBAT_READ;
      #endif
      if (batt_pin >= 0) {
        pinMode(batt_pin, INPUT);
        analogReadResolution(12); // 12-bit resolution for better accuracy
        // Set ADC attenuation to 11dB (0-3.3V range) for ESP32-S3
        // This is important for proper voltage measurement
        #ifdef ESP32
          analogSetPinAttenuation(batt_pin, ADC_11db);
        #endif
      }
    #endif

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      // Check if wake-up was due to EXT0 (DIO1 - LoRa packet)
      esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
      if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        startup_reason = BD_STARTUP_RX_PACKET;
      }
      
      // GPIO 39 (P_LORA_DIO_1) is not an RTC pin on ESP32-S3, so no need to release RTC holds
      // GPIO will be re-initialized normally after wakeup
    }
    // Note: Light sleep doesn't cause reset, so wakeup cause is checked in enterLightSleep()
    // and startup_reason is set there directly
  }

  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1, bool enable_radio_wakeup = true) {
    // ESP32-S3: Use ext0 wakeup for DIO1 (works with any GPIO pin)
    // For sensor-only mode, disable radio wakeup to save power
    
    // Disable WiFi and Bluetooth before deep sleep to save power
    #ifdef ESP_PLATFORM
      WiFi.mode(WIFI_OFF);
      btStop();
      // Disable watchdog timer - it can prevent deep sleep
      esp_task_wdt_deinit();
    #endif
    
    // Configure power domains for lowest power consumption
    // Keep only RTC peripherals and memory needed for wakeup
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
    // Power down other domains to save power
    esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_CPU, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
    
    // Disable USB to save power (USB can prevent deep sleep or consume power)
    // On ESP32-S3, USB Serial connection might prevent deep sleep
    // According to Seeed Studio wiki: https://wiki.seeedstudio.com/XIAO_ESP32S3_Consumption/
    // Always flush Serial before ending to ensure all data is sent
    Serial.flush();
    delay(100); // Give Serial time to finish all operations
    Serial.end();
    delay(100); // Additional delay after Serial.end() to ensure USB is fully disabled
    
    // Set USB pins (GPIO 19 = D-, GPIO 20 = D+) to INPUT with pull-down
    // USB-Serial-JTAG pins can cause leakage if left floating
    #ifdef CONFIG_IDF_TARGET_ESP32S3
      pinMode(19, INPUT_PULLDOWN); // USB D-
      pinMode(20, INPUT_PULLDOWN); // USB D+
      delay(10);
    #endif
    
    if (enable_radio_wakeup) {
      gpio_num_t wakeup_pin = (gpio_num_t)P_LORA_DIO_1;
      
      if (pin_wake_btn >= 0) {
        // If user button is specified, use it instead (only one pin supported by ext0)
        wakeup_pin = (gpio_num_t)pin_wake_btn;
      }

      gpio_set_direction(wakeup_pin, GPIO_MODE_INPUT);
      // Enable wakeup on rising edge (when DIO1 goes HIGH on LoRa packet RX)
      esp_sleep_enable_ext0_wakeup(wakeup_pin, 1);
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // CRITICAL: Enable GPIO deep sleep hold to maintain pin states during deep sleep
    // This prevents GPIO pins from floating and causing leakage current
    // Without this, pins may change state and cause power consumption issues
    #ifdef ESP_PLATFORM
      gpio_deep_sleep_hold_en();
    #endif

    // Small delay to ensure all operations complete
    delay(100);
    
    // Finally set ESP32 into sleep
    // Expected power consumption:
    // - ESP32-S3 module alone: ~8-20 µA in deep sleep
    // - XIAO ESP32S3 dev board: ~2-8 mA (due to power LED, voltage regulator, etc.)
    // Note: Dev board components (power LED, voltage regulator) cannot be disabled via software
    // For lowest power consumption, measure directly on ESP32-S3 module or desolder power LED
    esp_deep_sleep_start();   // CPU halts here and never returns!
  }

  void enterLightSleep(uint32_t secs, int pin_wake_btn = -1) {
    // Light sleep implementation for ESP32-S3
    // Light sleep allows radio to stay in RX mode and wake up faster than deep sleep
    
    // Flush Serial to ensure all data is sent before sleep
    Serial.flush();
    delay(100);  // Give UART time to finish all operations
    
    // Configure wakeup pin
    gpio_num_t wakeup_pin = (gpio_num_t)P_LORA_DIO_1;
    if (pin_wake_btn >= 0) {
      wakeup_pin = (gpio_num_t)pin_wake_btn;
    }
    
    // CRITICAL: Remove GPIO interrupt handler before light sleep
    // RadioLib interrupt handler conflicts with GPIO wakeup during light sleep
    // First disable the interrupt, then remove the handler
    gpio_intr_disable(wakeup_pin);
    gpio_isr_handler_remove(wakeup_pin);
    delay(50);  // Give time for interrupt to fully stop
    
    // Configure GPIO for wakeup (light sleep works with any GPIO, not just RTC pins)
    gpio_set_direction(wakeup_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(wakeup_pin, GPIO_PULLDOWN_ONLY);
    
    // Disable any existing wakeup sources first
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    
    // Disable any existing GPIO wakeup on this pin
    gpio_wakeup_disable(wakeup_pin);
    
    // Enable GPIO wakeup on high level (when DIO1 goes HIGH on LoRa packet RX)
    // HIGH_LEVEL is more reliable than POSEDGE for light sleep
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable(wakeup_pin, GPIO_INTR_HIGH_LEVEL);
    
    // Enable timer wakeup if specified
    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }
    
    // Enter light sleep (this will return after wakeup, unlike deep sleep)
    esp_light_sleep_start();
    
    // After wakeup, RadioLib will re-attach its interrupt handler when needed
    // We don't need to manually re-enable interrupts here
    
    // Check the wakeup cause and set startup_reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
      // For light sleep with GPIO wakeup, check if DIO1 pin is HIGH
      // This indicates a LoRa packet was received
      if (gpio_get_level(wakeup_pin) == 1) {
        startup_reason = BD_STARTUP_RX_PACKET;
      } else {
        startup_reason = BD_STARTUP_NORMAL;
      }
    } else {
      // Clear startup reason if not GPIO wakeup
      startup_reason = BD_STARTUP_NORMAL;
    }
    
    // Disable wakeup sources for next sleep
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (secs > 0) {
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
  }

  void powerOff() override {
    enterDeepSleep(0);
  }

  uint16_t getBattMilliVolts() override {
    // Battery voltage measurement on A0 pin with 1/2 voltage divider
    // Based on Seeed Studio wiki: https://wiki.seeedstudio.com/check_battery_voltage/
    // The battery voltage is divided by 1/2 with 200kΩ resistors and connected to A0
    // Use A0 constant if available (as per Seeed Studio wiki), otherwise use PIN_VBAT_READ
    #if defined(A0) || defined(PIN_VBAT_READ)
      // Determine which pin to use
      #if defined(A0)
        int pin = A0;  // Use A0 constant as per Seeed Studio wiki
      #else
        int pin = PIN_VBAT_READ;
      #endif
      
      // Ensure ADC is properly configured
      analogReadResolution(12); // 12-bit resolution
      
      // Average 16 readings to remove spike-like errors during communication
      // as recommended by Seeed Studio wiki
      uint32_t Vbatt_raw = 0;
      for (int i = 0; i < 16; i++) {
        // analogReadMilliVolts() returns voltage on pin in mV (already corrected by ESP32)
        // With 1/2 divider, pin voltage = battery_voltage / 2
        uint32_t pin_mv = analogReadMilliVolts(pin);
        Vbatt_raw += pin_mv;
        delay(1); // Small delay between readings for stability
      }
      
      // Calculate average pin voltage (in mV)
      uint32_t avg_pin_voltage_mv = Vbatt_raw / 16;
      
      // Attenuation ratio is 1/2, so multiply by 2 to get actual battery voltage
      // Result is in mV
      uint32_t battery_mv = 2 * avg_pin_voltage_mv;
      
      // If reading is suspiciously low (< 50mV), it might indicate:
      // 1. No voltage divider connected (pin sees noise only)
      // 2. Wrong pin number
      // 3. Pin not properly configured
      // For now, return the actual value even if low, so we can debug
      // (Previously returned 0 if < 50mV, but that hides the actual reading)
      
      // Clamp to reasonable range (0-5000mV = 0-5V) to avoid overflow
      if (battery_mv > 5000) battery_mv = 5000;
      
      return (uint16_t)battery_mv;
    #else
      // Fallback to parent implementation if neither A0 nor PIN_VBAT_READ defined
      return ESP32Board::getBattMilliVolts();
    #endif
  }

  const char* getManufacturerName() const override {
    return "Xiao S3 WIO";
  }
};
