#include <osv/jumpmu.hh>
// #include "TaskManager.hpp"

#include <signal.h>
// -------------------------------------------------------------------------------------
namespace jumpmu
{
    __thread JumpMUContext *thread_local_jumpmu_ctx __attribute__((tls_model("initial-exec"))) = nullptr;

#ifdef NEW_JUMPMU
    __thread JumpMUContext thread_local_jumpmu;
#endif
} // namespace jumpmu

