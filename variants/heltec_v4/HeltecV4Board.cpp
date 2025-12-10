#include "HeltecV4Board.h"
#include "target.h"

void HeltecV4Board::begin() {
    ESP32Board::begin();


    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW); // Initially inactive

    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER,HIGH);

    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_EN);
    pinMode(P_LORA_PA_EN, OUTPUT);
    digitalWrite(P_LORA_PA_EN,HIGH);
    pinMode(P_LORA_PA_TX_EN, OUTPUT);
    digitalWrite(P_LORA_PA_TX_EN,LOW);


    periph_power.begin();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  void HeltecV4Board::onBeforeTransmit(void) {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    digitalWrite(P_LORA_PA_TX_EN, HIGH);
  }

  void HeltecV4Board::onAfterTransmit(void) {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
    digitalWrite(P_LORA_PA_TX_EN, LOW);
  }

  void HeltecV4Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_EN); //It also needs to be enabled in receive mode

    if (pin_wake_btn < 0) {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet
    } else {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet OR wake btn
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // Finally set ESP32 into sleep
    esp_deep_sleep_start();   // CPU halts here and never returns!
  }

  void HeltecV4Board::enterLightSleep(uint32_t secs, int pin_wake_btn) {
    // Light sleep implementation for Heltec V4
    // Light sleep allows radio to stay in RX mode and wake up faster than deep sleep
    
    // Turn off display if present (before cutting power)
    #ifdef DISPLAY_CLASS
      display.turnOff();
    #endif
    
    // Power down peripherals to save power during light sleep
    // Display and other peripherals are powered via periph_power pin
    periph_power.release();
    
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
    
    // CRITICAL: Ensure pin is NOT in RTC mode, otherwise gpio_wakeup_enable might not work
    // rtc_gpio_deinit() also releases any RTC GPIO holds, so no need for separate rtc_gpio_hold_dis()
    rtc_gpio_deinit(wakeup_pin);
    
    // Configure GPIO for wakeup (light sleep works with any GPIO, including RTC pins)
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

  void HeltecV4Board::powerOff()  {
    enterDeepSleep(0);
  }

  uint16_t HeltecV4Board::getBattMilliVolts()  {
    analogReadResolution(10);
    digitalWrite(PIN_ADC_CTRL, HIGH);
    delay(10);
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / 8;

    digitalWrite(PIN_ADC_CTRL, LOW);

    return (5.42 * (3.3 / 1024.0) * raw) * 1000;
  }

  const char* HeltecV4Board::getManufacturerName() const {
    return "Heltec V4";
  }
