#include <Arduino.h>
#include "../../myLib/SoftTimer.h"
#include "../../myLib/GetTimeDiv.h"


void adcSetup(){

}

void onTimerAdcRead(){

}

//SoftTimer Timer1(1100, onTimerEnd, false);

void setup() {
  Serial.begin(115200);
  delay(500);

//  Timer1.start();
}

void loop() {
//  Timer1.update();
}

