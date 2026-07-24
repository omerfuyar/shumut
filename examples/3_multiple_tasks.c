#define SHU_IMPLEMENTATION
#include "../shumut.h"

#define COUNTER_LIMIT 31
usz counter = 0;

SHUSlice test1(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{
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

    return cs0;
}

SHUSlice test2(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{

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

    return cs0;
}

int main(int argc, char **argv)
{
    SHUThread thread;
    SHUTask task1, task2;
    SHUSlice ret1 = {.size = 1}, ret2 = {.size = 1};

    SHU_LogInfo("Main thread counter : %zu", counter);
    SHU_CheckPanic(SHU_ThreadCreate(&thread));
    SHU_CheckPanic(SHU_TaskCreate(&task1, thread, 0, test1, cs0, &ret1));
    SHU_CheckPanic(SHU_TaskCreate(&task2, thread, 0, test2, cs0, &ret2));

    SHU_LogInfo("main thread waiting");

    while (ret1.size != 0 || ret2.size != 0)
    {
    }

    SHU_LogInfo("main thread exiting, counter %zu", counter);

    return 0;
}
