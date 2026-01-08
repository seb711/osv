#include <osv/trace.hh>
#include <osv/interrupt.hh>
#include <osv/leanstore_debug.hh>
#include <osv/sched.hh>
#include <osv/rcu.hh>
#include "arch/x64/apic.hh"

TRACEPOINT(trace_leanstore_mutex_lock, "mut=%d \t\tpid=%d", int, int);
TRACEPOINT(trace_leanstore_mutex_unlock, "mut=%d \t\tpid=%d", int, int);
TRACEPOINT(trace_leanstore_mutex_wait_unlock, "mut=%d \t\tpid=%d", int, int);
TRACEPOINT(trace_leanstore_mutex_wait_lock, "mut=%d \t\tpid=%d", int, int);
TRACEPOINT(trace_leanstore_mutex_try_lock2, "mut=%p \t\tpid=%p", void *, void *);
TRACEPOINT(trace_leanstore_mutex_try_lock, "mut=%p \t\tpid=%p", void *, void *);
TRACEPOINT(trace_leanstore_finish, "pid=%d", int);

namespace leanstore_osv_debug
{
    extern "C" void trace_lock(int id, int pid)
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
    extern "C" void trace_unlock(int id, int pid)
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

        if (!b) {
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
}