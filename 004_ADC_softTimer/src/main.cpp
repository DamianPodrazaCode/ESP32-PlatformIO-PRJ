#include <Arduino.h>
#include "../../myLib/SoftTimer.h"
#include "../../myLib/GetTimeDiv.h"
#include "driver/adc.h"

void adcSetup() {
  // 9 - 12 (bits)
  analogReadResolution(12);

  // 1 - 255 (div)
  analogSetClockDiv(1);

  // ADC_0db - 100 mV ~ 950 mV
  // ADC_2_5db - 100 mV ~ 1250 mV
  // ADC_6db - 150 mV ~ 1750 mV
  // ADC_11db - 150 mV ~ 2450 mV
  analogSetAttenuation(ADC_11db);

  //void analogSetPinAttenuation(uint8_t pin, adc_attenuation_t attenuation);
  //bool adcAttachPin(uint8_t pin);
  //void analogSetWidth(uint8_t bits);
  //void analogSetVRefPin(uint8_t pin);
  //int hallRead();
}

// uint16_t analogRead(uint8_t pin);
// uint32_t analogReadMilliVolts(uint8_t pin);

void onTimerAdcRead() {
  uint32_t adc_mV = analogReadMilliVolts(GPIO_NUM_32);
  Serial.println(adc_mV);
}

GetTimeDiv tDiv;
SoftTimer TimerADC(500, onTimerAdcRead, false);

void setup() {
  Serial.begin(115200);
  delay(500);
  adcSetup();
  TimerADC.start();
}

void loop() {
  TimerADC.update();
}

