#define SHU_IMPLEMENTATION
#include "../shumut.h"

SHULock lock;

SHUSlice test(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{
    SHU_LockWait(lock);
    SHU_LogInfo("task function executing");
    SHU_LockRelease(lock); //? comment out to test if lock is working
    return cs((u8 *)0xDEAD, 31);
}

int main(int argc, char **argv)
{
    SHUThread thread;
    SHUTask task;
    SHUSlice returnValue;

    SHU_CheckPanic(SHU_LockCreate(&lock));
    SHU_CheckPanic(SHU_ThreadCreate(&thread));
    SHU_CheckPanic(SHU_TaskCreate(&task, thread, 0, test, cs0, &returnValue));

    SHU_LogInfo("main thread waiting");
    SHU_LockWait(lock);
    SHU_LogInfo("main thread exiting, return value : (%p, %zu)", returnValue.data, returnValue.size);

    return 0;
}
