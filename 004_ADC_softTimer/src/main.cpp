#include <Arduino.h>
#include "../../myLib/SoftTimer.h"
#include "../../myLib/GetTimeDiv.h"


void adcSetup() {

}

void onTimerAdcRead() {

}

GetTimeDiv tDiv;
SoftTimer TimerADC(500, onTimerAdcRead, false);

void setup() {
  Serial.begin(115200);
  delay(500);

  TimerADC.start();
}

void loop() {
  TimerADC.update();
}

