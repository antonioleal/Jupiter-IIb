#include "kernel.h"
#include <circle/startup.h>
#include <circle/sysconfig.h>

// The kernel object lives on the startup stack (KERNEL_STACK_SIZE): keep big buffers
// out of it (on the heap), or it overflows the stack and nothing starts
static_assert(sizeof(CKernel) <= KERNEL_STACK_SIZE / 4, "CKernel too big for the stack");

int main(void)
{
    CKernel Kernel;
    if (!Kernel.Initialize()) {
        return EXIT_HALT;
    }

    TShutdownMode ShutdownMode = Kernel.Run();

    return ShutdownMode;
}
