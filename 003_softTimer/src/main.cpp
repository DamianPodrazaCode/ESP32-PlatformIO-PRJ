#include <Arduino.h>

// ---------------------------------------------------------------
class SoftTimer {
private:
  typedef void (*CallbackFunction)();
  CallbackFunction callback;
  uint32_t interval;
  volatile uint32_t prevTime;
  volatile uint32_t nowTime;
  bool f_start;  //główna flaga włączająca timer

public:
  /*
  timeInterval - intrerwał dla CallbackFunction
  cb - nazwa CallbackFunction
  OnOff - timer startuje od razu(true) lub po wywołaniu metody start
  */
  SoftTimer(uint32_t timeInterval, CallbackFunction cb, bool OnOff) {
    f_start = false;
    callback = cb;
    interval = timeInterval;
    if (OnOff) start();
  }

  void start() {
    prevTime = millis();
    f_start = true;
  }

  void stop() {
    f_start = false;
  }

  void restart(uint32_t newTimeInterval, CallbackFunction cb, bool OnOff) {
    SoftTimer(newTimeInterval, cb, OnOff);
  }

  void update() {
    if (f_start) {
      nowTime = millis();
      if ((prevTime + interval) <= nowTime) {
        callback();
        prevTime = nowTime;
      }
    }
  }
};

// ---------------------------------------------------------------

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

