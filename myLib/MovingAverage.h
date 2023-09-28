#ifndef MovingAverage_h
#define MovingAverage_h

#include <Arduino.h>

template <class V>
class MovingAverage
{
private:
    uint32_t countData;
    uint32_t sizeData;
    V *dataTab;

public:
    MovingAverage(uint32_t size)
    {
        countData = 0;
        sizeData = size;
        dataTab = new V[size];
    }

    ~MovingAverage()
    {
        //delete[] dataTab;
    }

    void update(V dataU)
    {
        dataTab[countData++] = dataU;
        if (countData > sizeData)
            countData = 0;
    }

    V get()
    {
        double dataOut = 0;
        for (uint32_t i = 0; i < sizeData; i++)
        {
            dataOut += dataTab[i];
        }
        return (V)(dataOut / sizeData);
    }
};

#endif // MovingAverage_h