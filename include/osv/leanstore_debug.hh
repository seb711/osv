#ifndef OSV_LEANSTORE_H_
#define OSV_LEANSTORE_H_

// TODO: Provide a more high-level C++ API, see shrinker.
#include <atomic>

namespace leanstore_osv_debug
{
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
void trace_try_lock(void* id, void* pid); 
void trace_try_lock2(void* id, void* pid); 
void trace_wait_lock(int id, int pid); 
void trace_lock(int id, int pid); 
void trace_unlock(int id, int pid); 
void trace_wait_unlock(int id, int pid); 
void trace_finish_transaction( int pid); 

void set_priority(double p);
void yield();

void* get_interrupt_stack(); 
void set_interrupt_stack(void* stack); 

class Waiter {
    public: 
    std::atomic<void*> thread; 

    Waiter(); 

    void wake(); 
    void wait() const; 
}; 

#ifdef __cplusplus
}
#endif

}
#endif
