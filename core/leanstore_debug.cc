#include <osv/trace.hh>
#include <osv/interrupt.hh>
#include <osv/leanstore_debug.hh>
#include <osv/sched.hh>
#include <osv/rcu.hh>
#include "arch/x64/apic.hh"

TRACEPOINT(trace_leanstore_mutex_lock, "mut=%p \t\tpid=%p", void *, void *);
TRACEPOINT(trace_leanstore_mutex_unlock, "mut=%p \t\tpid=%p", void *, void *);
TRACEPOINT(trace_leanstore_mutex_wait, "mut=%p \t\tpid=%p", void *, void *);
TRACEPOINT(trace_leanstore_mutex_wait_unlock, "mut=%d \t\tpid=%d", int, int);
TRACEPOINT(trace_leanstore_mutex_wait_lock, "mut=%d \t\tpid=%d", int, int);
TRACEPOINT(trace_leanstore_mutex_try_lock2, "mut=%p \t\tpid=%p", void *, void *);
TRACEPOINT(trace_leanstore_mutex_try_lock, "mut=%p \t\tpid=%p", void *, void *);
TRACEPOINT(trace_leanstore_finish, "pid=%d", int);

TRACEPOINT(trace_leanstore_interrupted, "pid=%p \t\tcounter=%d", void*, int);
TRACEPOINT(trace_leanstore_interrupt_core, "core_from=%d \t\t core_to=%d \t\tcounter=%d", int, int, int);

TRACEPOINT(trace_prealloc_stack_pop, "stack=%p entry=%p idx=%d stack_pos=%d pop_count=%d ret_count=%d", void *, void *, int, int, int, int);
TRACEPOINT(trace_prealloc_stack_ret, "stack=%p entry=%p idx=%d stack_pos=%d pop_count=%d ret_count=%d", void *, void *, int, int, int, int);
TRACEPOINT(trace_prealloc_stack_error_invalid_ptr, "stack=%p entry=%p stack_pos=%d pop_count=%d ret_count=%d", void *, void *, int, int, int);
TRACEPOINT(trace_prealloc_stack_error_double_pop, "stack=%p entry=%p idx=%d stack_pos=%d pop_count=%d ret_count=%d", void *, void *, int, int, int, int);
TRACEPOINT(trace_prealloc_stack_error_double_ret, "stack=%p entry=%p idx=%d stack_pos=%d pop_count=%d ret_count=%d", void *, void *, int, int, int, int);
TRACEPOINT(trace_prealloc_stack_error_overflow, "stack=%p entry=%p stack_pos=%d size=%d pop_count=%d ret_count=%d", void *, void *, int, int, int, int);

TRACEPOINT(trace_leanstore_scheduling_params, "iopoll=%d \tio_submit=%d \tpageprov=%d \tnic=%d", int, int, int, int);
TRACEPOINT(trace_leanstore_calc_freq, "id=%d \t\twork=%g \t\tfreq=%d", int, float, int); 

namespace leanstore_osv_debug
{
    extern "C" void trace_interrupted(void* p, int counter) {
        trace_leanstore_interrupted(p, counter); 
    }
    extern "C" void trace_prealloc_pop(void *stack, void *entry, int idx, int stack_pos, int pop_count, int ret_count)
    {
        trace_prealloc_stack_pop(stack, entry, idx, stack_pos, pop_count, ret_count);
    }

    extern "C" void trace_prealloc_ret(void *stack, void *entry, int idx, int stack_pos, int pop_count, int ret_count)
    {
        trace_prealloc_stack_ret(stack, entry, idx, stack_pos, pop_count, ret_count);
    }

    extern "C" void trace_prealloc_error_invalid_ptr(void *stack, void *entry, int stack_pos, int pop_count, int ret_count)
    {
        trace_prealloc_stack_error_invalid_ptr(stack, entry, stack_pos, pop_count, ret_count);
    }

    extern "C" void trace_prealloc_error_double_pop(void *stack, void *entry, int idx, int stack_pos, int pop_count, int ret_count)
    {
        trace_prealloc_stack_error_double_pop(stack, entry, idx, stack_pos, pop_count, ret_count);
    }

    extern "C" void trace_prealloc_error_double_ret(void *stack, void *entry, int idx, int stack_pos, int pop_count, int ret_count)
    {
        trace_prealloc_stack_error_double_ret(stack, entry, idx, stack_pos, pop_count, ret_count);
    }

    extern "C" void trace_prealloc_error_overflow(void *stack, void *entry, int stack_pos, int size, int pop_count, int ret_count)
    {
        trace_prealloc_stack_error_overflow(stack, entry, stack_pos, size, pop_count, ret_count);
    }
    extern "C" void trace_wait(void *id, void *pid)
    {
        trace_leanstore_mutex_wait(id, pid);
    }
    extern "C" void trace_lock(void *id, void *pid)
    {
        trace_leanstore_mutex_lock(id, pid);
    }
    extern "C" void trace_try_lock2(void *id, void *pid)
    {
        trace_leanstore_mutex_try_lock2(id, pid);
    }
    extern "C" void trace_try_lock(void *id, void *pid)
    {
        trace_leanstore_mutex_try_lock(id, pid);
    }
    extern "C" void trace_wait_lock(int id, int pid)
    {
        trace_leanstore_mutex_wait_lock(id, pid);
    }
    extern "C" void trace_unlock(void *id, void *pid)
    {
        trace_leanstore_mutex_unlock(id, pid);
    }

    extern "C" void trace_wait_unlock(int id, int pid)
    {
        trace_leanstore_mutex_wait_unlock(id, pid);
    }

    extern "C" void trace_finish_transaction(int pid)
    {
        trace_leanstore_finish(pid);
    }

    extern "C" void set_priority(double p)
    {
        sched::thread::current()->set_priority(p);
    }

    extern "C" void yield()
    {
        sched::thread::current()->yield();
    }

    extern "C" Waiter::Waiter()
    {
        thread = sched::thread::current();
    }

    extern "C" void Waiter::wake()
    {
        ((sched::thread *)thread.load(std::memory_order_relaxed))->wake_with_from_mutex([&]
                                                                                        { thread.store(nullptr, std::memory_order_release); });
    }

    extern "C" void Waiter::wait() const
    {
        sched::thread::wait_until([&]
                                  { return !thread.load(std::memory_order_acquire); });
    }

    extern "C" void *get_interrupt_stack()
    {
        return sched::current_cpu->arch.get_ist_entry(2);
    }

    extern "C" void set_interrupt_stack(void *stack)
    {
        sched::current_cpu->arch.set_ist_entry(3, (char *)stack, 8192);
    }

    extern "C" void disable_scheduler()
    {
        sched::current_cpu->scheduler_using = false;
    }
    static inline uint64_t rdtsc()
    {
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
    }
    extern "C" void create_watchdog(uint64_t time, int cpuid, int vec, std::atomic<uint64_t> &timestamp)
    {
        static bool b = false;

        if (!b)
        {
            auto *cpu = sched::current_cpu;
            (new sched::thread(
                 [cpu, vec, time, &timestamp]
                 {
                     while (true)
                     {
                         uint64_t ts = timestamp.load();
                         if (ts > 0 and (rdtsc() - ts) > (time * 5))
                         {
                             // printf("interrupt\n");
                             timestamp = 0;
                             processor::apic->ipi(cpu->arch.apic_id, vec);
                             // usleep(INTERRUPT_TIME / 500);
                             // usleep(1);
                             asm volatile("pause" : : : "memory");
                         }
                     }
                 },
                 sched::thread::attr().pin(sched::cpus[cpuid]).name("watchdog")))
                ->start();
            b = true;
        }
    }

    extern "C" void send_watchdog_ipi(int vec, int cpuid) {
        trace_leanstore_interrupt_core(sched::current_cpu->id, cpuid, vec); 
        processor::apic->ipi(sched::cpus[cpuid]->arch.apic_id, vec);
    }

    extern "C" void trace_leanstore_states(int iopoll, int iosubmit, int pageprovider, int nic) {
        trace_leanstore_scheduling_params(iopoll, iosubmit, pageprovider, nic); 
    }

    extern "C" void trace_leanstore_sched_comp(int id, double work, int freq) {
        trace_leanstore_calc_freq(id, work, freq); 
    }
}