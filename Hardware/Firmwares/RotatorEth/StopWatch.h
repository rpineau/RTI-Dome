// Rodolphe Pineau
// Easy timer to check for time passed between a reset and elapsed call
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
