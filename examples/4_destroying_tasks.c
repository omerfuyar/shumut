#define SHU_IMPLEMENTATION
#include "../shumut.h"

#define COUNTER_INTERVAL 1000

_Atomic usz working = 0;

SHUSlice test(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{
    usz counter = 0;
    while (true)
    {
        counter++;

        if (counter == (COUNTER_INTERVAL * 2))
        {
            SHU_AtomicWrite(&working, 1);
        }

        if ((counter % COUNTER_INTERVAL) == 0)
        {
            SHU_LogInfo("worker waiting to be killed : %zu", counter);
        }
    }

    return cs0;
}

int main(int argc, char **argv)
{
    SHUThread thread;
    SHUTask task;
    SHUSlice ret = {0};

    SHU_LogInfo("Main thread spawning worker thread");

    SHU_AssertResult(SHU_ThreadCreate(&thread));
    SHU_AssertResult(SHU_TaskCreate(&task, thread, 0, test, cs0, &ret));

    while (SHU_AtomicRead(&working) == 0)
    {
    }

    SHU_LogInfo("Worker thread created, now destroying it");

    SHU_TaskDestroy(thread, task);

    SHU_LogInfo("Main thread exited");

    return 0;
}
