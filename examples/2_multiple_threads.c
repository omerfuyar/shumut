#define SHU_IMPLEMENTATION
#include "../shumut.h"

#define THREAD_COUNT 4

_Atomic usz counter = 0;
const char *const threadNames[] = {"Abu Bakr", "Umar", "Uthman", "Ali"};

SHUSlice test(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{
    usz threadID = argument.size;
    SHU_LogInfo("task function %zu executing", threadID);
    SHU_AtomicSum(&counter, 1);
    return cs((void *)threadNames[threadID % 4], threadID);
}

int main(int argc, char **argv)
{
    SHUThread threads[THREAD_COUNT];
    SHUTask tasks[THREAD_COUNT];
    SHUSlice returnValues[THREAD_COUNT];

    SHU_LogInfo("main thread spawning others");
    for (usz i = 0; i < THREAD_COUNT; i++)
    {
        SHU_CheckPanic(SHU_ThreadCreate(&threads[i]));
        SHU_CheckPanic(SHU_TaskCreate(&tasks[i], threads[i], 0, test, cs(NULL, i), &returnValues[i]));
    }

    SHU_LogInfo("main thread waiting");

    while (SHU_AtomicRead(&counter) != THREAD_COUNT)
    {
    }

    SHU_ThreadSleep(100); // to return from task function

    SHU_LogInfo("main thread exiting, counter : %zu", counter);

    for (usz i = 0; i < THREAD_COUNT; i++)
    {
        SHU_LogInfo("return value for thread %zu : %s", returnValues[i].size, (char *)returnValues[i].data);
    }

    return 0;
}
