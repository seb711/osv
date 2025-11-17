#ifndef LW_THREADS_H_
#define LW_THREADS_H_

#include <functional>
#include <vector>
#include <cstddef>

#ifdef __cplusplus
extern "C"
{
#endif
// here we first of all need the job_thread_pool the static one
void switch_lw_thread(void* thread); 
void *allocate_osv_thread(std::function<void()> func, size_t id); 
void disable_interrupts(); 
void enable_interrupts(); 
void* get_current_thread(); 
void scheduler_enable(); 
void scheduler_disable();
void cleanup_osv_thread(void*);  
#ifdef __cplusplus
}
#endif

#endif
