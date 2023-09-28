#ifndef MovingAverage_h
#define MovingAverage_h

#include <Arduino.h>

class MovingAverage {
private:

public:
    MovingAverage(int8_t *data, uint32_t size) {}
    MovingAverage(int16_t *data, uint32_t size) {}
    MovingAverage(int32_t *data, uint32_t size) {}
    MovingAverage(double *data, uint32_t size) {}
};

#endif // MovingAverage_h