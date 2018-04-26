#pragma once

#include <chrono>
#include <condition_variable>
#include <thread>


    
class CondWait {
public:
    void signal();
    int wait(std::recursive_mutex& mutex);
    int waitTimeout(std::recursive_mutex& mutex, unsigned int ms);

private:
    std::condition_variable_any cpp_cond;
};
