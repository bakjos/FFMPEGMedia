
#include "CondWait.h"


    
void CondWait::signal() {
    cpp_cond.notify_one();
}

int CondWait::wait(std::recursive_mutex& mutex) {
    return waitTimeout(mutex, 0);
}

int CondWait::waitTimeout(std::recursive_mutex& mutex, unsigned int ms) {
    try {
        std::unique_lock<std::recursive_mutex> cpp_lock(mutex, std::adopt_lock_t());
        if (ms == 0) {
            cpp_cond.wait(cpp_lock);
            cpp_lock.release();
            return 0;
        }
        else {
            auto wait_result = cpp_cond.wait_for(cpp_lock,std::chrono::milliseconds(ms));
            cpp_lock.release();
            if (wait_result == std::cv_status::timeout) {
                //TIMEOUT
                return 1;
            }
            else {
                return 0;
            }
        }
    }
    catch (const std::system_error & ) {
        //OFX_LOGP(ofx_error, "unable to wait on a C++ condition variable: code=%d; %s", ex.code(), ex.what());
        return -1;
    }
}
