
#include "CondWait.h"
#include "GenericPlatform/GenericPlatformProcess.h"

CondWait::CondWait() {
    event = FGenericPlatformProcess::GetSynchEventFromPool();
}

CondWait::~CondWait() {
    if ( event ) {
        FGenericPlatformProcess::ReturnSynchEventToPool(event);
    }
}
    
void CondWait::signal() {
    if ( event) {
        event->Trigger();
    }
}

int CondWait::wait(FCriticalSection& mutex) {
    return waitTimeout(mutex, 0);
}

int CondWait::waitTimeout(FCriticalSection& mutex , unsigned int ms) {

    mutex.Unlock();
    if ( ms == 0) {
        event->Wait();
        mutex.Lock();
        return 0;
    } else {
        bool wait_result =  event->Wait(FTimespan::FromMicroseconds(ms));
        mutex.Lock();
        if ( !wait_result) {
            return 1;
        }
        return 0;
    }
}
