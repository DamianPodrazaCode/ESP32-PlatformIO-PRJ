#ifndef SoftTimer_h
#define SoftTimer_h

//#include <Arduino.h>
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

  // uruchomienie timera jeżeli nie został uruchomiony przy tworzeniu obiektu
  void start() {
    prevTime = millis();
    f_start = true;
  }

  // zatrzymanie timera
  void stop() {
    f_start = false;
  }

  // restart timera
  void restart(uint32_t newTimeInterval, CallbackFunction cb, bool OnOff) {
    SoftTimer(newTimeInterval, cb, OnOff);
  }

  // metoda która musi być uruchomiona w głównej pętli programu żeby timer wogóle działał
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
#endif /* SoftTimer_h */