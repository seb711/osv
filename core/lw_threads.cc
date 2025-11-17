#include <osv/lw_threads.hh>
#include <osv/sched.hh>
#include <iostream> 

extern "C" void *allocate_osv_thread(std::function<void()> func, size_t id)
    {
        static sched::thread_pool<7000, 4096 * 2>  leanstore_pool; // TODO: Use PERCPU.

        sched::thread *t = leanstore_pool.allocate(func, sched::thread::attr().name("helper_" + std::to_string(id)));
        if (t == nullptr) {
            return nullptr; 
        }
       
        t->_detached_state->_cpu = sched::cpu::current();
        t->_detached_state->st.store(sched::thread::status::running);
        t->setup_minimal(); 
        t->_attr._lazy_register = false;
        return t;
    }

extern "C" void switch_lw_thread(void *t)
    {
        sched::thread* real_t = (sched::thread*) t;
        
        // sched::thread::current()->_detached_state->st.store(sched::thread::status::queued);
        auto prev_t = sched::thread::current();

        // ((sched::thread*) t)->_runtime.hysteresis_run_start();
        real_t->_detached_state->st.store(sched::thread::status::running);
        real_t->switch_to_specialized();

        prev_t->_detached_state->st.store(sched::thread::status::queued);
    }

extern "C" void disable_interrupts() {
    // arch::irq_disable(); 
}
extern "C" void enable_interrupts() {
    // arch::irq_enable(); 
}

extern "C" void scheduler_disable() {
    sched::cpu::current()->scheduler_using.store(false);
}

extern "C" void scheduler_enable() {
    sched::cpu::current()->scheduler_using.store(true);
}

extern "C" void* get_current_thread() {
    return (void*) sched::thread::current(); 
};

extern "C" void cleanup_osv_thread(void* t) {

    return ((sched::thread*) t)->_cleanup(); 
}
