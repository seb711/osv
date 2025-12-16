#include <osv/trace.hh>
#include <osv/interrupt.hh>
#include <osv/leanstore_debug.hh>
#include <osv/sched.hh>
#include <osv/rcu.hh>
TRACEPOINT(trace_leanstore_mutex_lock, "mut=%d \t\tpid=%d",  int, int);
TRACEPOINT(trace_leanstore_mutex_unlock, "mut=%d \t\tpid=%d",  int, int);
TRACEPOINT(trace_leanstore_mutex_wait_unlock, "mut=%d \t\tpid=%d",  int, int);
TRACEPOINT(trace_leanstore_mutex_wait_lock, "mut=%d \t\tpid=%d",  int, int);
TRACEPOINT(trace_leanstore_mutex_try_lock, "mut=%d \t\tpid=%d",  int, int);
TRACEPOINT(trace_leanstore_finish, "pid=%d", int);


namespace leanstore_osv_debug
{
    extern "C" void trace_lock(int id, int pid) {
    trace_leanstore_mutex_lock(id, pid); 
}
extern "C" void trace_try_lock(int id, int pid) {
    trace_leanstore_mutex_try_lock(id, pid); 
}
extern "C" void trace_wait_lock(int id, int pid) {
    trace_leanstore_mutex_wait_lock(id, pid); 
}
extern "C" void trace_unlock(int id, int pid) {
    trace_leanstore_mutex_unlock(id, pid); 
}

extern "C" void trace_wait_unlock(int id, int pid) {
    trace_leanstore_mutex_wait_unlock(id, pid); 
}

extern "C" void trace_finish_transaction(int pid) {
    trace_leanstore_finish(pid); 
}

extern "C"  void set_priority(double p) {
    sched::thread::current()->set_priority(p); 
}

extern "C"  void yield() {
    sched::thread::current()->yield(); 
}


extern "C"  Waiter::Waiter() {
    thread = sched::thread::current(); 
}

extern "C"  void Waiter::wake() {
    ((sched::thread*) thread.load(std::memory_order_relaxed))->wake_with_from_mutex([&] { thread.store(nullptr, std::memory_order_release); });
}

extern "C"  void Waiter::wait() const {
    sched::thread::wait_until([&] { return !thread.load(std::memory_order_acquire); });
}
}