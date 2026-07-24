#define SHU_IMPLEMENTATION
#include "../shumut.h"

#define COUNTER_INTERVAL 1000

bool working = false;

SHUSlice test(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{
    usz counter = 0;
    while (true)
    {
        counter++;

        if (counter == (COUNTER_INTERVAL * 2))
        {
            working = true;
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

    SHU_CheckPanic(SHU_ThreadCreate(&thread));
    SHU_CheckPanic(SHU_TaskCreate(&task, thread, 0, test, cs0, &ret));

    while (!working)
    {
    }

    SHU_LogInfo("Worker thread created, now destroying it");

    SHU_CheckPanic(SHU_ThreadDestroy(thread));

    SHU_LogInfo("Main thread exited");

    return 0;
}
