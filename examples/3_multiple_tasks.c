#define SHU_IMPLEMENTATION
#include "../shumut.h"

#define COUNTER_LIMIT 32
usz counter = 0;
SHULock lock;

SHUSlice
test1(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{
    SHU_LockTry(lock);

    while (true)
    {
        counter += 1;
        SHU_LogInfo("Task 1 increments counter by 1 : %zu", counter);
        if (counter > COUNTER_LIMIT)
        {
            break;
        }
        yield;
    }

    SHU_LockRelease(lock);

    return cs0;
}

SHUSlice test2(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{
    SHU_LockTry(lock);

    while (true)
    {
        counter += 2;
        SHU_LogInfo("Task 2 increments counter by 2 : %zu", counter);
        if (counter > COUNTER_LIMIT)
        {
            break;
        }
        yield;
    }

    SHU_LockRelease(lock);

    return cs0;
}

int main(int argc, char **argv)
{
    SHUThread thread;
    SHUTask task1, task2;

    SHU_CheckPanic(SHU_LockCreate(&lock));

    SHU_LogInfo("Main thread counter : %zu", counter);
    SHU_CheckPanic(SHU_ThreadCreate(&thread));
    SHU_CheckPanic(SHU_TaskCreate(&task1, thread, 0, test1, cs0, NULL));
    SHU_CheckPanic(SHU_TaskCreate(&task2, thread, 0, test2, cs0, NULL));

    SHU_LogInfo("main thread waiting");

    SHU_LockWait(lock);

    SHU_LogInfo("main thread exiting");

    return 0;
}
