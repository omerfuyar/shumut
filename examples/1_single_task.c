#define SHU_IMPLEMENTATION
#include "../shumut.h"

_Atomic usz done = 0;

SHUSlice test(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{
    SHU_LogInfo("task function executing");
    SHU_AtomicWrite(&done, 1);
    return cs((void *)0xDEAD, 31);
}

int main(int argc, char **argv)
{
    SHUThread thread;
    SHUTask task;
    SHUSlice returnValue;

    SHU_LogInfo("main thread spawning other");

    SHU_AssertResult(SHU_ThreadCreate(&thread));
    SHU_AssertResult(SHU_TaskCreate(&task, thread, 0, test, cs0, &returnValue));

    SHU_LogInfo("main thread waiting");
    while (SHU_AtomicRead(&done) == 0)
    {
    }

    SHU_ThreadSleep(100); // to return from task function

    SHU_LogInfo("main thread exiting, return value : (%p, %zu)", returnValue.data, returnValue.size);

    return 0;
}
