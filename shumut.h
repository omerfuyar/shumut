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

/// @brief
typedef struct SHUI_Thread *SHUThread;

/// @brief
typedef struct SHUI_Task *SHUTask;

/// @brief
typedef struct SHUI_Lock *SHULock;

/// @brief
typedef SHUSlice (*SHUExecutionFunction)(SHUThread thisThread, SHUTask thisTask, SHUSlice argument);

SHUThread SHU_ThreadGetCurrent(void);

SHUResult SHU_ThreadCreate(SHUThread *retThread);

SHUResult SHU_ThreadDestroy(SHUThread thread);

void SHU_ThreadClear(SHUThread thread);

SHUTask SHU_TaskGetCurrent(void);

SHUResult SHU_TaskCreate(SHUTask *retTask, SHUThread thread, usz stackSize, SHUExecutionFunction function, SHUSlice argument);

void SHU_TaskDestroy(SHUTask task);

void SHU_TaskYield(SHUTask task);

#define yield SHU_TaskYield(SHU_TaskGetCurrent())

SHUResult SHU_LockCreate(SHULock *retLock);

void SHU_LockDestroy(SHULock lock);

bool SHU_LockTry(SHULock lock);

void SHU_LockRelease(SHULock lock);

#pragma endregion Declarations

#pragma region Definitions

#ifdef SHU_IMPLEMENTATION

#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef LPVOID SHUIContext;
#else
#include <pthread.h>
#include <ucontext.h>
typedef ucontext_t SHUIContext;
#endif

#pragma region Internals

typedef enum SHUISignal
{
    SHUISignal_Stop = 1 << 0,
} SHUISignal;

typedef struct SHUI_Thread
{
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t handle;
#endif
    SHUIContext context;
    u8 signals;
    SHUTask headTask;
} SHUI_Thread;

typedef struct SHUI_Task
{
    // header
    SHUIContext context;
    u8 signals;
    SHUTask next;
    SHUExecutionFunction function;
    SHUSlice argument;
    SHUSlice returnValue;
    usz stackSize;
    // stack
} SHUI_Task;

typedef struct SHUI_Lock
{
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
} SHUI_Lock;

static _Thread_local SHUThread SHUI_CURRENT_THREAD = NULL;
static _Thread_local SHUTask SHUI_CURRENT_TASK = NULL;

/*
static void SHUI_GetCurrentContext(SHUIContext *retContext)
{
#ifdef _WIN32
    *retContext = GetCurrentFiber();
    SHU_Assert(*retContext != NULL, "Getting thread context failed.");
#else
    SHU_Assert(!getcontext(retContext), "Getting thread context failed.");
#endif
}
*/

static void SHUI_JumpToContext(SHUIContext *fromContext, SHUIContext *toContext)
{
#ifdef _WIN32
    SwitchToFiber(*toContext);
#else
    swapcontext(fromContext, toContext);
#endif
}

/// parameter is the initialized SHUTask that will be started
#ifdef _WIN32
static VOID WINAPI SHUI_TaskFunctionWrap(LPVOID parameter)
{
    SHUTask task = (SHUTask)parameter;
#else
static void SHUI_TaskFunctionWrap(int upper, int lower)
{
    uintptr_t ptr = ((uintptr_t)upper << (sizeof(int) * 8)) | (uintptr_t)(unsigned int)lower;
    SHUTask task = (SHUTask)ptr;
#endif
    task->returnValue = task->function(SHUI_CURRENT_THREAD, task, task->argument);
    task->signals = SHUISignal_Stop;
    SHUI_JumpToContext(&task->context, &SHUI_CURRENT_THREAD->context);
}

static void SHUI_CreateContext(SHUIContext *retContext, SHUTask task)
{
#ifdef _WIN32
    *retContext = CreateFiber(task->stackSize, SHUI_TaskFunctionWrap, task);
    SHU_Assert(*retContext != NULL, "Creating thread context failed.");
#else
    getcontext(retContext);
    (*retContext).uc_stack.ss_sp = (char *)task + sizeof(SHUI_Task); //! after the header
    (*retContext).uc_stack.ss_size = task->stackSize;
    (*retContext).uc_link = NULL;

    uintptr_t ptr = (uintptr_t)task;
    int upper = (int)(ptr >> (sizeof(int) * 8));
    int lower = (int)(ptr & (uintptr_t)((int)0 - (int)1));
    makecontext(retContext, (void (*)(void))SHUI_TaskFunctionWrap, 2, upper, lower);
#endif
}

/// parameter is the initialized SHUThread that will be started
#ifdef _WIN32
static DWORD WINAPI SHUI_ThreadFunctionWrap(LPVOID parameter)
#else
static void *SHUI_ThreadFunctionWrap(void *parameter)
#endif
{
    SHUThread thread = (SHUThread)parameter;

    SHUI_CURRENT_THREAD = thread;
    SHUI_CURRENT_TASK = thread->headTask;

#ifdef _WIN32
    thread->context = ConvertThreadToFiber(NULL);
    SHU_Assert(thread->context != NULL, "Converting thread to fiber failed.");
#endif

    while (thread->signals == 0)
    {
        SHUI_JumpToContext(&thread->context, &SHUI_CURRENT_TASK->context);
        SHUI_CURRENT_TASK = SHUI_CURRENT_TASK->next;
    }

    return 0;
}

static SHUResult SHUI_SpawnThreadWithTask(SHUThread thread, SHUTask task)
{
    SHU_CheckPanicNullPointer(thread);
    SHU_CheckPanicNullPointer(task);

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
    SHU_CheckPanicNullPointer(retThread);

    SHUThread thread = (SHUThread)malloc(sizeof(SHUI_Thread));
    if (thread == NULL)
    {
        return SHUResult_ErrAllocation;
    }
    memset(thread, 0x00, sizeof(SHUI_Thread));

    *retThread = thread;

    return SHUResult_Ok;
}

SHUResult SHU_ThreadDestroy(SHUThread thread)
{
    SHU_CheckPanicNullPointer(thread);

    thread->signals = SHUISignal_Stop; // todo atomic

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

    SHU_ThreadClear(thread);
    free(thread);

    return SHUResult_Ok;
}

void SHU_ThreadClear(SHUThread thread)
{
    SHU_CheckPanicNullPointer(thread);

    if (thread->headTask != NULL)
    {
        SHUTask tempTask = thread->headTask;
        SHUTask tempNext = tempTask->next;

        while (tempNext != thread->headTask)
        {
            tempTask = tempNext;
            tempNext = tempTask->next;

            SHU_TaskDestroy(tempTask);
        }

        SHU_TaskDestroy(thread->headTask);
    }
}

SHUTask SHU_TaskGetCurrent(void)
{
    return SHUI_CURRENT_TASK;
}

SHUResult SHU_TaskCreate(SHUTask *retTask, SHUThread thread, usz stackSize, SHUExecutionFunction function, SHUSlice argument)
{
    SHU_CheckPanicNullPointer(retTask);
    SHU_CheckPanicNullPointer(thread);
    SHU_CheckPanicNullPointer(function);

    stackSize = stackSize == 0 ? SHUC_DEFAULT_TASK_STACK_CAPACITY : stackSize;

    usz tempStackSize = stackSize;
#ifdef _WIN32
    tempStackSize = 0;
#endif

    SHUTask task = (SHUTask)malloc(sizeof(SHUI_Task) + tempStackSize);
    if (task == NULL)
    {
        return SHUResult_ErrAllocation;
    }
    memset(task, 0x00, sizeof(SHUI_Task) + tempStackSize);

    task->argument = argument;
    task->function = function;
    task->stackSize = stackSize;
    task->returnValue = cs0;
    SHUI_CreateContext(&task->context, task);

    if (thread->headTask == NULL) // init
    {
        SHU_CheckReturn(SHUI_SpawnThreadWithTask(thread, task),
                        free(task););
    }
    else // append
    {
        SHUTask tempTask = thread->headTask;
        while (tempTask->next != thread->headTask)
        {
            tempTask = tempTask->next;
        }
        tempTask->next = task;
        task->next = thread->headTask;
    }

    *retTask = task;
    return SHUResult_Ok;
}

void SHU_TaskDestroy(SHUTask task)
{
    SHU_CheckPanicNullPointer(task);
    free(task);
}

void SHU_TaskYield(SHUTask task)
{
    SHU_CheckPanicNullPointer(task);
    SHUI_JumpToContext(&task->context, &SHUI_CURRENT_THREAD->context);
}

SHUResult SHU_LockCreate(SHULock *retLock)
{
    SHU_CheckPanicNullPointer(retLock);

    SHULock lock = (SHULock)malloc(sizeof(SHUI_Lock));
    if (lock == NULL)
    {
        return SHUResult_ErrAllocation;
    }
    memset(lock, 0x00, sizeof(SHUI_Lock));

#ifdef _WIN32
    InitializeCriticalSection(&lock->mutex);
#else
    pthread_mutex_init(&lock->mutex, NULL);
#endif

    *retLock = lock;

    return SHUResult_Ok;
}

void SHU_LockDestroy(SHULock lock)
{
    SHU_CheckPanicNullPointer(lock);

#ifdef _WIN32
    DeleteCriticalSection(&lock->mutex);
#else
    pthread_mutex_destroy(&lock->mutex);
#endif

    free(lock);
}

bool SHU_LockTry(SHULock lock)
{
    SHU_CheckPanicNullPointer(lock);

    // return 0 if entered cs
#ifdef _WIN32
    return !TryEnterCriticalSection(&lock->mutex);
#else
    return !pthread_mutex_trylock(&lock->mutex);
#endif
}

void SHU_LockRelease(SHULock lock)
{
    SHU_CheckPanicNullPointer(lock);

#ifdef _WIN32
    LeaveCriticalSection(&lock->mutex);
#else
    pthread_mutex_unlock(&lock->mutex);
#endif
}

#endif // SHU_IMPLEMENTATION

#pragma endregion Definitions
