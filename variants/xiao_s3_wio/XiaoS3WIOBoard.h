#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

class XiaoS3WIOBoard : public ESP32Board {
public:
  XiaoS3WIOBoard() { }

  void begin() {
    ESP32Board::begin();

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

  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1) {
    // ESP32-S3: Use ext0 wakeup for DIO1 (works with any GPIO pin)
    gpio_num_t wakeup_pin = (gpio_num_t)P_LORA_DIO_1;
    
    if (pin_wake_btn >= 0) {
      // If user button is specified, use it instead (only one pin supported by ext0)
      wakeup_pin = (gpio_num_t)pin_wake_btn;
    }

    gpio_set_direction(wakeup_pin, GPIO_MODE_INPUT);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    
    // Enable wakeup on rising edge (when DIO1 goes HIGH on LoRa packet RX)
    esp_sleep_enable_ext0_wakeup(wakeup_pin, 1);

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // Finally set ESP32 into sleep
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

  const char* getManufacturerName() const override {
    return "Xiao S3 WIO";
  }
};
