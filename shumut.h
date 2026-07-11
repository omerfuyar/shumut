#pragma once

#ifndef SHU_HEADER
#ifdef SHU
#include SHU
#else
#include "../shu/shu.h"
#endif
#endif

#pragma region Macros

#define SHUC_DEFAULT_THREAD_STACK_CAPACITY (1024 * 1024)
#define SHUC_DEFAULT_TASK_STACK_CAPACITY (128 * 1024)

#pragma endregion Macros

#pragma region Declarations

/// @brief Handle for thread objects to spawn tasks from.
typedef struct SHUI_Thread *SHUThread;

/// @brief Handle for task objects to manage tasks.
typedef struct SHUI_Task *SHUTask;

/// @brief Handle for lock objects across threads and tasks inside threads.
typedef struct SHUI_Lock *SHULock;

/// @brief Function signature for creating new tasks.
typedef SHUSlice (*SHUExecutionFunction)(SHUThread thisThread, SHUTask thisTask, SHUSlice argument);

/// @brief Gets the currently running thread handle.
/// @return Handle of currently running thread. NULL for the main thread.
SHUThread SHU_ThreadGetCurrent(void);

/// @brief Creates an OS thread which can spawn tasks.
/// @param retThread Thread handle to use.
/// @return ErrAllocation
SHUResult SHU_ThreadCreate(SHUThread *retThread);

/// @brief Destroys an OS thread together with its spawned tasks.
/// @param thread Thread to destroy.
/// @return ErrInternal
SHUResult SHU_ThreadDestroy(SHUThread thread);

/// @brief Destroys all of the spawned tasks of a thread.
/// @param thread Thread to clear.
void SHU_ThreadClear(SHUThread thread);

/// @brief Gets the currently running task handle.
/// @return Handle of currently running task. NULL for the main thread.
SHUTask SHU_TaskGetCurrent(void);

/// @brief Creates a task belong to a thread created previously.
/// @param retTask Task handle to use.
/// @param thread Thread to spawn task from.
/// @param stackSize Stack size of the created task.
/// @param function Function to execute on task.
/// @param argument Argument to pass to task function.
/// @param retReturnAddress Return address of the task. Leave NULL if not needed.
/// @return ErrAllocation / ErrInternal
SHUResult SHU_TaskCreate(SHUTask *retTask, SHUThread thread, usz stackSize, SHUExecutionFunction function, SHUSlice argument, SHUSlice *retReturnAddress);

/// @brief Destroys a task and removes it from thread execution queue.
/// @param task Task to destroy.
void SHU_TaskDestroy(SHUTask task);

/// @brief Yield a task to its thread, leaving its execution to another task.
/// @param task Task to yield.
/// @note Don't forget to call this function in a task function, otherwise one task will block all others in the same thread.
void SHU_TaskYield(SHUTask task);

/// @brief Yield the current task.
#define yield SHU_TaskYield(SHU_TaskGetCurrent())

/// @brief Creates a lock to use tasks across / inside threads.
/// @param retLock Lock handle to use.
/// @return ErrAllocation / SHUResult_ErrInternal
SHUResult SHU_LockCreate(SHULock *retLock);

/// @brief Destroys a lock.
/// @param lock Lock to destroy.
void SHU_LockDestroy(SHULock lock);

/// @brief Checks and tries to acquire a lock.
/// @param lock Lock to try acquire.
/// @return True if lock is acquired, false if lock is already locked.
/// @note This function is not blocking. Don't forget to yield if the lock is already locked in a task function. If you want a blocking option, see `SHU_LockWait`.
bool SHU_LockTry(SHULock lock);

/// @brief Checks and waits to acquire a lock.
/// @param lock Lock to wait for acquiring.
/// @note This function is blocking. So be aware that this function will block all other tasks running in its thread. If you want a non-blocking option, see `SHU_LockTry`.
void SHU_LockWait(SHULock lock);

/// @brief Unlocks a lock, leaving its ownership.
/// @param lock Lock to release.
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
    SHUISignal_Stop,
    SHUISignal_Finished,
} SHUISignal;

typedef struct SHUI_Thread
{
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t handle;
#endif
    SHUIContext context;
    SHUISignal signals;
    SHUTask headTask;
    SHUTask tailTask;
} SHUI_Thread;

typedef struct SHUI_Task
{
    // header
    SHUIContext context;
    SHUISignal signals;
    SHUTask next;
    SHUExecutionFunction function;
    SHUSlice argument;
    SHUSlice *returnAddress;
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

static void SHUI_JumpToContext(SHUIContext *fromContext, SHUIContext *toContext)
{
#ifdef _WIN32
    SwitchToFiber(*toContext);
#else
    SHU_Assert(!swapcontext(fromContext, toContext), "Swapping thread context failed.");
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
    SHUSlice result = task->function(SHUI_CURRENT_THREAD, task, task->argument);
    if (task->returnAddress != NULL)
    {
        *task->returnAddress = result;
    }

    task->signals = SHUISignal_Finished;
    SHUI_JumpToContext(&task->context, &SHUI_CURRENT_THREAD->context);
}

static void SHUI_CreateContext(SHUIContext *retContext, SHUTask task)
{
#ifdef _WIN32
    *retContext = CreateFiber(task->stackSize, SHUI_TaskFunctionWrap, task);
    SHU_Assert(*retContext != NULL, "Creating thread context failed.");
#else
    SHU_Assert(!getcontext(retContext), "Getting thread context failed.");
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
        SHUTask nextTask = SHUI_CURRENT_TASK->next;
        SHUI_JumpToContext(&thread->context, &SHUI_CURRENT_TASK->context);

        switch (SHUI_CURRENT_TASK->signals)
        { // todo signals, check for edge cases, like all tasks finishing, thread left empty etc. remove from list if stopped / finished.
        case SHUISignal_Stop:
            break;
        case SHUISignal_Finished:
            break;
        default:
            break;
        }

        SHUI_CURRENT_TASK = nextTask;
    }

    return 0;
}

static SHUResult SHUI_SpawnThreadWithTask(SHUThread thread, SHUTask task)
{
    SHU_CheckPanicNullPointer(thread);
    SHU_CheckPanicNullPointer(task);

    thread->headTask = task;
    thread->tailTask = task;
    task->next = task;

#ifdef _WIN32
    thread->handle = CreateThread(NULL, SHUC_DEFAULT_THREAD_STACK_CAPACITY, SHUI_ThreadFunctionWrap, thread, 0, NULL);

    if (thread->handle == NULL)
    {
        return SHUResult_ErrInternal;
    }
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (pthread_attr_setstacksize(&attr, SHUC_DEFAULT_THREAD_STACK_CAPACITY))
    {
        return SHUResult_ErrInternal;
    }

    if (pthread_create(&thread->handle, &attr, SHUI_ThreadFunctionWrap, thread))
    {
        return SHUResult_ErrInternal;
    }

    pthread_attr_destroy(&attr);
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

    if (!ConvertFiberToThread())
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
            tempNext = tempTask->next;

            SHU_TaskDestroy(tempTask);

            tempTask = tempNext;
        }

        SHU_TaskDestroy(thread->headTask);
    }
}

SHUTask SHU_TaskGetCurrent(void)
{
    return SHUI_CURRENT_TASK;
}

SHUResult SHU_TaskCreate(SHUTask *retTask, SHUThread thread, usz stackSize, SHUExecutionFunction function, SHUSlice argument, SHUSlice *retReturnAddress)
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
    task->returnAddress = retReturnAddress;
    SHUI_CreateContext(&task->context, task);

    if (thread->headTask == NULL) // init
    {
        SHU_CheckReturn(SHUI_SpawnThreadWithTask(thread, task),
                        free(task););
    }
    else // append
    {
        thread->tailTask = task;
        task->next = thread->headTask;
    }

    *retTask = task;
    return SHUResult_Ok;
}

void SHU_TaskDestroy(SHUTask task)
{
    SHU_CheckPanicNullPointer(task);
    free(task);
    // todo unlink
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
    if (pthread_mutex_init(&lock->mutex, NULL))
    {
        return SHUResult_ErrInternal;
    }
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

#ifdef _WIN32
    return TryEnterCriticalSection(&lock->mutex);
#else
    return pthread_mutex_trylock(&lock->mutex);
#endif
}

void SHU_LockWait(SHULock lock)
{
    SHU_CheckPanicNullPointer(lock);

#ifdef _WIN32
    EnterCriticalSection(&lock->mutex);
#else
    pthread_mutex_lock(&lock->mutex);
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
