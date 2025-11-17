#include <osv/lw_threads.hh>
#include <cstddef>

extern "C" void *allocate_osv_thread(std::function<void()> func, size_t id)
    {
        return nullptr;
    }

extern "C" void switch_lw_thread(void *t)
    {
       return; 
    }

extern "C" void disable_interrupts() {}; 
extern "C" void enable_interrupts() {}; 
extern "C" void* get_current_thread() {}; 
extern "C" void scheduler_disable() {
}

extern "C" void scheduler_enable() {
}

extern "C" void cleanup_osv_thread(void* t) {
}