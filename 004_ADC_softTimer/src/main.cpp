#include <Arduino.h>
#include "../../myLib/SoftTimer.h"
#include "../../myLib/GetTimeDiv.h"
#include "../../myLib/MovingAverage.h"
#include "driver/adc.h"

// piny analog (ADC2 używany jest do WiFi)
// GPIO 4 - ADC2 CH 0
// GPIO 0 - ADC2 CH 1
// GPIO 2 - ADC2 CH 2
// GPIO 15 - ADC2 CH 3
// GPIO 13 - ADC2 CH 4
// GPIO 12 - ADC2 CH 5
// GPIO 14 - ADC2 CH 6
// GPIO 27 - ADC2 CH 7
// GPIO 26 - ADC2 CH 9
// GPIO 25 - ADC2 CH 8
// GPIO 33 - ADC1 CH 5
// GPIO 32 - ADC1 CH 4
// GPIO 35 - ADC1 CH 7
// GPIO 34 - ADC1 CH 6
// GPIO 39 - ADC1 CH 3
// GPIO 36 - ADC1 CH 0

void adcSetup()
{
  // 9 - 12 (bits)
  analogReadResolution(12);

  // 1 - 255 (div)
  analogSetClockDiv(1);

  // ADC_0db - 100 mV ~ 950 mV
  // ADC_2_5db - 100 mV ~ 1250 mV
  // ADC_6db - 150 mV ~ 1750 mV
  // ADC_11db - 150 mV ~ 2450 mV
  analogSetAttenuation(ADC_11db);

  // void analogSetPinAttenuation(uint8_t pin, adc_attenuation_t attenuation);
  // bool adcAttachPin(uint8_t pin);
  // void analogSetWidth(uint8_t bits);
  // void analogSetVRefPin(uint8_t pin);
  // int hallRead();
}

// This function is used to get the ADC raw value for a given pin/ADC channel.
// uint16_t analogRead(uint8_t pin);

// This function is used to get ADC value for a given pin/ADC channel in millivolts.
// uint32_t analogReadMilliVolts(uint8_t pin);

GetTimeDiv tDiv;
MovingAverage<double> mAVR(32);

void onTimerAdcRead()
{
  tDiv.start();
  uint32_t adc_mV = analogReadMilliVolts(GPIO_NUM_32);
  mAVR.update(adc_mV);
  tDiv.end();
  Serial.print(adc_mV);
  Serial.print(" ");
  Serial.print(mAVR.get());
  Serial.print(" ");
  Serial.println(tDiv.getLastDiv());
}

SoftTimer TimerADC(100, onTimerAdcRead, false);

void setup()
{
  Serial.begin(115200);
  delay(500);
  adcSetup();
  TimerADC.start();
}

void loop()
{
  TimerADC.update();
}
