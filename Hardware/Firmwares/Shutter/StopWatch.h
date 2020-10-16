//
// RTI-Zone Dome Shutter firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed a lot to it and it's being deprecated, I'm now using it for myself.
//


#ifndef StopWatch_h
#define StopWatch_h

class StopWatch
{
public:
    StopWatch();
    void reset();
    unsigned long elapsed();

private:
    unsigned long _starttime;
};

StopWatch::StopWatch()
{
    reset();
}

void StopWatch::reset()
{
    _starttime = millis();
}

unsigned long StopWatch::elapsed()
{
    return millis() - _starttime;
}

#endif
// END OF FILE
