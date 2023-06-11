#ifndef GetTimeDiv_h
#define GetTimeDiv_h

#include <Arduino.h>

class GetTimeDiv {

private:
    uint32_t divTime;
    uint32_t tempTime;
    uint32_t divTimeMicros;
    uint32_t tempTimeMicros;

public:
    void start() {
        tempTime = millis();
    }

    void end() {
        divTime = millis() - tempTime;
    }

    void startMicros() {
        tempTimeMicros = micros();
    }

    void endMicros() {
        divTimeMicros = micros() - tempTimeMicros;
    }

    uint32_t getLastDiv() {
        return divTime;
    }

    uint32_t getLastDivMicros() {
        return divTimeMicros;
    }
};

#endif // GetTimeDiv_h