#include <Arduino.h>
#include "SoftTimer.h"

uint32_t ticks = 0;
void onTimerEnd() {
  Serial.print("Timer1 tick ");
  Serial.print(ticks);
  Serial.print(" ");
  Serial.println((volatile uint32_t)millis());
  ticks++;
}

SoftTimer Timer1(1100, onTimerEnd, false);

void setup() {
  Serial.begin(115200);
  delay(500);

  Timer1.start();
}

void loop() {
  Timer1.update();
  if (ticks > 20) {
    Serial.println("Timer1 stop");
    Timer1.stop();
    ticks = 0;
  }
}

