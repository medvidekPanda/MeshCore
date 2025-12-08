#include <Arduino.h>
#include "target.h"

HeltecV4Board board;

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  
#if defined(P_LORA_SCLK)
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) {
  // Heltec V4 with GC1109 PA + 17dB attenuator (non-linear gain curve)
  // Based on forum discussions and Meshtastic implementation
  // 
  // Heltec V4 mapping:
  // - Value 10 dBm (into PA) = 22 dBm total output
  // - For 27 dBm total: set internal SX1262 to 22 dBm (max), external PA adds boost
  // - SX1262 max is 22 dBm, external PA adds ~5 dB for total 27 dBm
  // 
  // CRITICAL: Never exceed 27 dBm total output (legal/regulatory limit)
  
  // Enforce maximum of 27 dBm to prevent exceeding legal limits
  if (dbm > 27) {
    dbm = 27;
  }
  
  // Automatic power reduction at low battery to prevent brownout
  // 27 dBm requires high current draw, which can cause voltage sag and brownout restart
  uint16_t batt_mv = board.getBattMilliVolts();
  if (batt_mv > 0) {
    if (batt_mv < 3600) {
      // Very low battery: limit to 22 dBm max
      if (dbm > 22) {
        dbm = 22;
      }
    } else if (batt_mv < 3800) {
      // Low battery: limit to 25 dBm max
      if (dbm > 24) {
        dbm = 24;
      }
    }
    // For battery > 3800 mV, allow full 27 dBm
  }
  
  uint8_t radiolib_power;
  if (dbm >= 27) {
    // For 27 dBm: set internal SX1262 to 22 dBm (max), external GC1109 PA adds boost
    radiolib_power = 22;  // SX1262 max, PA boosts to 27 dBm total (max legal limit)
  } else if (dbm == 22) {
    // For 22 dBm: use value 10 (10 dBm into PA = 22 dBm total output)
    radiolib_power = 10;  // 10 dBm into PA = 22 dBm total output
  } else if (dbm > 22 && dbm < 27) {
    // For 23-26 dBm: interpolate between 10 and 22
    radiolib_power = 10 + ((dbm - 22) * 12 / 5);  // Linear interpolation
    if (radiolib_power > 22) radiolib_power = 22;  // Cap at max
  } else {
    // For lower values: direct mapping
    radiolib_power = dbm;
  }
  radio.setOutputPower(radiolib_power);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
