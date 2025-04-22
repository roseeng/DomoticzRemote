#include <Arduino.h>

class Interval
{
    private:
    unsigned long lastMillis = 0;

    public:
    bool Every(int intervalSeconds)
    {
        if (millis() > lastMillis + intervalSeconds*1000 ) {
            lastMillis = millis();
            return true;
        } else {
            return false;
        }
    }
};