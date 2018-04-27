#pragma once

#include "HAL/Event.h"
#include "ScopeLock.h"
#include <chrono>



    
class CondWait {
public:
    CondWait();
    ~CondWait();
    void signal();
    int wait(FCriticalSection& mutex);
    int waitTimeout(FCriticalSection& mutex, unsigned int ms);

private:
    FEvent* event;
};
