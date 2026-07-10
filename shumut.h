#pragma once

#ifndef SHU_HEADER
#ifdef SHU
#include SHU
#else
#include "../shu/shu.h"
#endif
#endif

#pragma region Macros

// #define SHUC_DEFAULT_THREAD_STACK_CAPACITY (2 * 1024 * 1024)
#define SHUC_DEFAULT_TASK_STACK_CAPACITY (8 * 1024 * 1024)

#pragma endregion Macros

#pragma region Declarations

typedef struct SHUI_Thread *SHUThread;
typedef struct SHUI_Task *SHUTask;
typedef struct SHUI_Lock *SHULock;

typedef SHUSlice (*SHUExecutionFunction)(SHUThread thisThread, SHUTask thisTask, SHUSlice argument);

SHUThread SHU_ThreadGetCurrent(void);

SHUResult SHU_ThreadCreate(SHUThread *retThread);

SHUResult SHU_ThreadDestroy(SHUThread thread);

SHUResult SHU_ThreadClear(SHUThread thread);

SHUTask SHU_TaskGetCurrent(void);

SHUResult SHU_TaskCreate(SHUTask *retTask, SHUThread thread, usz stackCapacity, SHUExecutionFunction function, SHUSlice argument);

SHUResult SHU_TaskDestroy(SHUTask task);

SHUResult SHU_TaskYield(SHUTask task);

#define yield SHU_TaskYield(SHU_TaskGetCurrent())

#pragma endregion Declarations

#pragma region Definitions

#ifdef SHU_IMPLEMENTATION

#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE SHUIThreadHandle;
typedef SRWLOCK SHUILockHandle;
typedef int SHUIContextHandle;
#else
#include <pthread.h>
typedef pthread_t SHUIThreadHandle;
typedef pthread_mutex_t SHUILockHandle;
typedef int SHUIContextHandle;
#endif

#pragma region Internals

typedef enum SHUI_Signal
{
    SHUI_Signal_Stop = 1 << 0,
} SHUI_Signal;

typedef struct SHUI_Thread
{
    u8 signals;
    SHUTask headTask;
    SHUIThreadHandle handle;
} SHUI_Thread;

typedef struct SHUI_Task
{
    // header
    u8 signals;
    SHUTask next;
    SHUExecutionFunction function;
    SHUSlice argument;
    SHUSlice returnValue;
    usz stackCapacity;
    // stack
} SHUI_Task;

typedef struct SHUI_Lock
{
    SHUILockHandle handle;
    /*
    this will be used for both threads and tasks as: so it will not block tasks individually, will yield to thread
    while (SHU_LockCheck(...))
    ...
    */
} SHUI_Lock;

static _Thread_local SHUThread SHUI_CURRENT_THREAD = NULL;
static _Thread_local SHUTask SHUI_CURRENT_TASK = NULL;

/// parameter is the initialized SHUThread that will be started
#ifdef _WIN32
static DWORD WINAPI SHUI_ThreadFunctionWrap(LPVOID parameter)
#else
static void *SHUI_ThreadFunctionWrap(void *parameter)
#endif
{
    SHUThread thread = (SHUThread)parameter;

    SHUI_CURRENT_THREAD = thread;
    SHUI_CURRENT_TASK = thread->headTask; // todo change on scheduler function

    // todo scheduler check signals, yields etc.

    thread->headTask->returnValue = thread->headTask->function(thread, thread->headTask, thread->headTask->argument);
    return 0;
}

#ifdef _WIN32
static DWORD WINAPI SHUI_TaskFunctionWrap(LPVOID parameter)
#else
static void *SHUI_TaskFunctionWrap(void *parameter)
#endif
{
    return 0;
}

static SHUResult SHUI_SpawnThreadWithTask(SHUThread thread, SHUTask task)
{
    if (thread == NULL || task == NULL)
    {
        return SHUResult_ErrNullPointer;
    }

    thread->headTask = task;
    task->next = thread->headTask;

#ifdef _WIN32
    thread->handle = CreateThread(NULL, 0, SHUI_ThreadFunctionWrap, thread, 0, NULL);

    if (thread->handle == NULL)
    {
        return SHUResult_ErrInternal;
    }
#else
    if (pthread_create(&thread->handle, NULL, SHUI_ThreadFunctionWrap, thread))
    {
        return SHUResult_ErrInternal;
    }
#endif

    return SHUResult_Ok;
}

#pragma endregion Internals

SHUThread SHU_ThreadGetCurrent(void)
{
    return SHUI_CURRENT_THREAD;
}

SHUResult SHU_ThreadCreate(SHUThread *retThread)
{
    if (retThread == NULL)
    {
        return SHUResult_ErrNullPointer;
    }

    SHUThread thread = (SHUThread)malloc(sizeof(SHUI_Thread));
    if (thread == NULL)
    {
        return SHUResult_ErrInternal;
    }
    memset(thread, 0x00, sizeof(SHUI_Thread));

    *retThread = thread;

    return SHUResult_Ok;
}

SHUResult SHU_ThreadDestroy(SHUThread thread)
{
    if (thread == NULL)
    {
        return SHUResult_ErrNullPointer;
    }

    thread->signals = SHUI_Signal_Stop; // todo atomic

#if defined(_WIN32)
    if (WaitForSingleObject(thread->handle, INFINITE) == WAIT_FAILED)
    {
        return SHUResult_ErrInternal;
    }

    if (!CloseHandle(thread->handle))
    {
        return SHUResult_ErrInternal;
    }
#else
    pthread_join(thread->handle, NULL);
#endif

    SHUResult result = SHU_ThreadClear(thread);
    if (result)
    {
        return result;
    }
    memset(thread, 0x00, sizeof(SHUI_Thread));
    free(thread);

    return SHUResult_Ok;
}

SHUResult SHU_ThreadClear(SHUThread thread)
{
    if (thread == NULL)
    {
        return SHUResult_ErrNullPointer;
    }

    if (thread->headTask != NULL)
    {
        SHUTask tempTask = thread->headTask;
        SHUTask tempNext = tempTask->next;
        SHUResult result = 0;

        while (tempNext != thread->headTask)
        {
            tempTask = tempNext;
            tempNext = tempTask->next;

            result = SHU_TaskDestroy(tempTask);
            if (result)
            {
                return result;
            }
        }

        result = SHU_TaskDestroy(thread->headTask);
        if (result)
        {
            return result;
        }
    }

    return SHUResult_Ok;
}

SHUTask SHU_TaskGetCurrent(void)
{
    return SHUI_CURRENT_TASK;
}

SHUResult SHU_TaskCreate(SHUTask *retTask, SHUThread thread, usz stackCapacity, SHUExecutionFunction function, SHUSlice argument)
{
    if (retTask == NULL || thread == NULL || function == NULL)
    {
        return SHUResult_ErrNullPointer;
    }

    SHUTask task = (SHUTask)malloc(sizeof(SHUI_Task) + stackCapacity);
    if (task == NULL)
    {
        return SHUResult_ErrInternal;
    }
    memset(task, 0x00, sizeof(SHUI_Task) + stackCapacity);

    if (thread->headTask == NULL) // init
    {
        SHUResult result = SHUI_SpawnThreadWithTask(thread, task);

        if (result)
        {
            return result;
        }
    }
    else // append
    {
        SHUTask tempTask = thread->headTask;
        while (tempTask->next != thread->headTask)
        {
            tempTask = tempTask->next;
        }
        tempTask->next = task;
    }

    if (stackCapacity == 0)
    {
        stackCapacity = SHUC_DEFAULT_TASK_STACK_CAPACITY;
    }
    task->argument = argument;
    task->function = function;
    task->stackCapacity = stackCapacity;
    task->returnValue = cs0;

    // todo proper scheduling

    *retTask = task;
    return SHUResult_Ok;
}

SHUResult SHU_TaskDestroy(SHUTask task)
{
    if (task == NULL)
    {
        return SHUResult_ErrNullPointer;
    }
    memset(task, 0x00, sizeof(SHUI_Task) + task->stackCapacity);
    free(task);
}

SHUResult SHU_TaskYield(SHUTask task)
{
    // todo handle returning and yielding from tasks
}

#endif // SHU_IMPLEMENTATION

#pragma endregion Definitions
