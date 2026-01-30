#include <osv/leanstore_debug.hh>

namespace leanstore_osv_debug
{
    extern "C" void trace_interrupted(void* p, int counter) {
    }
    extern "C" void trace_prealloc_pop(void *stack, void *entry, int idx, int stack_pos, int pop_count, int ret_count)
    {
    }

    extern "C" void trace_prealloc_ret(void *stack, void *entry, int idx, int stack_pos, int pop_count, int ret_count)
    {
    }

    extern "C" void trace_prealloc_error_invalid_ptr(void *stack, void *entry, int stack_pos, int pop_count, int ret_count)
    {
    }

    extern "C" void trace_prealloc_error_double_pop(void *stack, void *entry, int idx, int stack_pos, int pop_count, int ret_count)
    {
    }

    extern "C" void trace_prealloc_error_double_ret(void *stack, void *entry, int idx, int stack_pos, int pop_count, int ret_count)
    {
    }

    extern "C" void trace_prealloc_error_overflow(void *stack, void *entry, int stack_pos, int size, int pop_count, int ret_count)
    {
    }
    extern "C" void trace_wait(void *id, void *pid)
    {
    }
    extern "C" void trace_lock(void *id, void *pid)
    {
    }
    extern "C" void trace_try_lock2(void *id, void *pid)
    {
    }
    extern "C" void trace_try_lock(void *id, void *pid)
    {
    }
    extern "C" void trace_wait_lock(int id, int pid)
    {
    }
    extern "C" void trace_unlock(void *id, void *pid)
    {
    }

    extern "C" void trace_wait_unlock(int id, int pid)
    {
    }

    extern "C" void trace_finish_transaction(int pid)
    {
    }

    extern "C" void set_priority(double p)
    {
    }

    extern "C" void yield()
    {
    }

    extern "C" Waiter::Waiter()
    {
    }

    extern "C" void Waiter::wake()
    {
    }

    extern "C" void Waiter::wait() const
    {
    }

    extern "C" void *get_interrupt_stack()
    {
    }

    extern "C" void set_interrupt_stack(void *stack)
    {
    }

    extern "C" void disable_scheduler()
    {
    }
    static inline uint64_t rdtsc()
    {
        return 0; 
    }

    extern "C" volatile void* get_shared_memory() {
        return nullptr; 
    }

    extern "C" void create_watchdog(uint64_t time, int cpuid, int vec, std::atomic<uint64_t> &timestamp)
    {
    }

    extern "C" void send_watchdog_ipi(int vec, int cpuid) {
    }

    extern "C" void trace_leanstore_states(int iopoll, int iosubmit, int pageprovider, int nic) {
    }

    extern "C" void trace_leanstore_sched_comp(int id, double work, int freq) {
    }
}