#define SHU_IMPLEMENTATION
#include "../shumut.h"

#define THREAD_COUNT 4

usz counter = 0;
SHULock counterLock;
const char *const threadNames[] = {"Abu Bakr", "Umar", "Uthman", "Ali"};

SHUSlice test(SHUThread thisThread, SHUTask thisTask, SHUSlice argument)
{
    usz threadID = argument.size;
    SHU_LockWait(counterLock);
    SHU_LogInfo("task function %zu executing", threadID);
    counter++;
    SHU_LockRelease(counterLock);
    return cs((u8 *)threadNames[threadID % 4], threadID);
}

int main(int argc, char **argv)
{
    SHU_CheckPanic(SHU_LockCreate(&counterLock));

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
    SHU_LockWait(counterLock);
    SHU_LogInfo("main thread exiting, counter : %zu", counter);

    for (usz i = 0; i < THREAD_COUNT; i++)
    {
        SHU_LogInfo("return value for thread %zu : %s", returnValues[i].size, returnValues[i].data);
    }

    return 0;
}
