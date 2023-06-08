#include <Arduino.h>

// ---------------------------------------------------------------
typedef void (*CallbackFunction)();

class SoftTimer {
private:
  CallbackFunction callback;
  uint32_t interval;
  uint32_t prevTime;
  uint32_t nowTime;
  bool f_start;

public:
  SoftTimer(uint32_t timeAlarm, CallbackFunction cb, bool OnOff) {
    f_start = false;
    callback = cb;
    if (OnOff) start();
  }

  void start() {
    f_start = true;
  }

  void stop() {
    f_start = false;
  }

  void restart(uint32_t newTimeAlarm, CallbackFunction cb, bool OnOff) {
    SoftTimer( newTimeAlarm, cb, OnOff);
  }

  void update() {
    if (f_start) {
      callback();
    }
  }
};

// ---------------------------------------------------------------

uint32_t ticks = 0;
void onTimerEnd() {
  Serial.print("Timer1 tick ");
  Serial.print(ticks);
  Serial.print(" ");
  Serial.println(millis());
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
    Serial.println("Timer 1 stop");
    Timer1.stop();
  }
}

