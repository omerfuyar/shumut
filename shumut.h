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
/// @param thread Thread to abort.
/// @return ErrInternal
SHUResult SHU_ThreadDestroy(SHUThread thread);

/// @brief Puts the current thread to sleep for specified time.
/// @param thread Thread to sleep.
/// @param milliseconds Milliseconds to sleep for.
void SHU_ThreadSleep(u64 milliseconds);

/// @brief Destroys all of the spawned tasks of a thread without destroying the thread itself.
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
/// @param thread Thread to destroy it from.
/// @param task Task to destroy.
void SHU_TaskDestroy(SHUThread thread, SHUTask task);

/// @brief Yield a task to its thread, leaving its execution to another task.
/// @param task Task to yield.
/// @param milliseconds Milliseconds to yield for. Pass 0 for instant queue.
/// @note Don't forget to call this function in a task function, otherwise one task will block all others in the same thread.
void SHU_TaskYield(SHUTask task, u64 milliseconds);

/// @brief Yield the current task.
#define yield SHU_TaskYield(SHU_TaskGetCurrent(), 0)

/// @brief Yield the current task for milliseconds.
/// @param milliseconds Milliseconds to delay this task.
#define yieldMilliseconds(milliseconds) SHU_TaskYield(SHU_TaskGetCurrent(), milliseconds)

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

/// @brief Atomic read operation. Use only with `usz` type.
/// @param atomicVariable Variable to read atomically.
/// @return The ridden value from atomic address.
usz SHU_AtomicRead(_Atomic usz *atomicVariable);

/// @brief Atomic write operation. Use only with `usz` type.
/// @param atomicVariable Variable to write atomically.
/// @param sourceVariable Value to write to variable.
void SHU_AtomicWrite(_Atomic usz *atomicVariable, usz writeValue);

/// @brief Atomic sum operation. Use only with `usz` type.
/// @param atomicVariable Variable to sum atomically.
/// @param valueToSum Value to sum to variable.
/// @return The old value before summation operation
usz SHU_AtomicAdd(_Atomic usz *atomicVariable, usz sumValue);

#pragma endregion Declarations

#pragma region Definitions

#ifdef SHU_IMPLEMENTATION

#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
typedef LPVOID SHUIContext;
#else
#include <unistd.h>
#include <pthread.h>
#include <ucontext.h>
typedef ucontext_t SHUIContext;
#endif

#pragma region Internals

typedef enum SHUISignal
{
    SHUISignal_None = 0 << 0,
    SHUISignal_Destroyed = 1 << 0,
    SHUISignal_Finished = 1 << 1,
    SHUISignal_Sleeping = 1 << 2,
} SHUISignal;

typedef struct SHUI_Thread
{
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t handle;
#endif
    SHUIContext context;
    _Atomic SHUISignal signals;
    SHUTask headTask;
    SHUTask tailTask;
    SHULock taskListLock;
} SHUI_Thread;

typedef struct SHUI_Task
{
    // header
    SHUIContext context;
    _Atomic SHUISignal signals;
    SHUTask next;
    SHUTask previous;
    SHUExecutionFunction function;
    SHUSlice argument;
    SHUSlice *returnAddress;
    usz stackSize;
    u64 wakeAt;
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

static _Thread_local struct
{
    SHUThread currentThread;
    SHUTask currentTask;
} SHUMUT = {0};

static u64 SHUI_GetMilliseconds(void)
{
#ifdef _WIN32
    return (u64)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000ULL + (u64)(ts.tv_nsec / 1000000);
#endif
}

static void SHUI_JumpToContext(SHUIContext *fromContext, SHUIContext *toContext)
{
#ifdef _WIN32
    SwitchToFiber(*toContext);
#else
    SHU_Assert(!swapcontext(fromContext, toContext), "Swapping thread context failed.");
#endif
}

static void SHUI_TaskUnlink(SHUThread thread, SHUTask task)
{
    if (SHUMUT.currentTask == task)
    {
        SHUMUT.currentTask = (task->next != task) ? task->next : NULL;
    }

    if (task->next == task)
    {
        thread->headTask = NULL;
        thread->tailTask = NULL;
        return;
    }

    task->previous->next = task->next;
    task->next->previous = task->previous;

    if (thread->headTask == task)
    {
        thread->headTask = task->next;
    }

    if (thread->tailTask == task)
    {
        thread->tailTask = task->previous;
    }
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
    SHUSlice result = task->function(SHUMUT.currentThread, task, task->argument);
    if (task->returnAddress != NULL)
    {
        *task->returnAddress = result;
    }

    SHU_AtomicWrite((_Atomic usz *)&task->signals, SHUISignal_Finished);
    SHUI_JumpToContext(&task->context, &SHUMUT.currentThread->context);
}

static void SHUI_CreateContext(SHUIContext *retContext, SHUTask task)
{
#ifdef _WIN32
    *retContext = CreateFiber(task->stackSize, SHUI_TaskFunctionWrap, task);
    SHU_Assert(*retContext != NULL, "Creating thread context failed.");
#else
    SHU_Assert(!getcontext(retContext), "Getting thread context failed.");
    uintptr_t rawBase = (uintptr_t)task + sizeof(SHUI_Task);
    uintptr_t alignedBase = (rawBase + 15) & ~(uintptr_t)15;
    usz padding = alignedBase - rawBase;

    (*retContext).uc_stack.ss_sp = (char *)alignedBase;
    (*retContext).uc_stack.ss_size = task->stackSize - padding;

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

    SHUMUT.currentThread = thread;
    SHUMUT.currentTask = thread->headTask;

#ifdef _WIN32
    thread->context = ConvertThreadToFiber(NULL);
    SHU_Assert(thread->context != NULL, "Setting up the thread %p failed.", thread->handle);
#else
    SHU_Assert(!(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) || pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL)),
               "Setting up the thread %p failed.", thread->handle);
#endif

    while (SHU_AtomicRead((_Atomic usz *)&thread->signals) == SHUISignal_None)
    {
        if (SHUMUT.currentTask == NULL)
        {
            if (thread->headTask == NULL)
            {
                continue; // no tasks left; wait for one to be appended
            }
            SHUMUT.currentTask = thread->headTask;
        }

        SHU_LockWait(thread->taskListLock);
        SHUTask next = SHUMUT.currentTask->next;
        SHU_LockRelease(thread->taskListLock);

        switch (SHU_AtomicRead((_Atomic usz *)&SHUMUT.currentTask->signals))
        {
        case SHUISignal_Destroyed:
        case SHUISignal_Finished:
            SHU_LockWait(thread->taskListLock);
            SHUI_TaskUnlink(thread, SHUMUT.currentTask);
            SHU_LockRelease(thread->taskListLock);
#ifdef _WIN32
            // DeleteFiber(SHUMUT.currentTask->context);
#endif
            free(SHUMUT.currentTask);
            break;
        case SHUISignal_Sleeping:
            if (SHUMUT.currentTask->wakeAt != 0 &&
                SHUI_GetMilliseconds() < SHUMUT.currentTask->wakeAt)
            {
                break;
            }
            SHUMUT.currentTask->wakeAt = 0;
            SHUMUT.currentTask->signals = SHUISignal_None;
        default:

            SHUI_JumpToContext(&thread->context, &SHUMUT.currentTask->context);
            break;
        }

        SHUMUT.currentTask = (thread->headTask != NULL) ? next : NULL;
    }

    switch (thread->signals)
    {
    default:
        break;
    }

#ifdef _WIN32
    SHU_Assert(ConvertFiberToThread(), "Cleaning up the thread %p failed.", thread->handle); // todo move to signals or something
#endif
    return 0;
}

static SHUResult SHUI_SpawnThreadWithTask(SHUThread thread, SHUTask task)
{
    thread->headTask = task;
    thread->tailTask = task;
    task->next = task;
    task->previous = task;

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
    return SHUMUT.currentThread;
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

void SHU_ThreadSleep(u64 milliseconds)
{
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(milliseconds / 1000);
    ts.tv_nsec = (long)((milliseconds % 1000) * 1000000);

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
    {
    }
#endif
}

SHUResult SHU_ThreadDestroy(SHUThread thread)
{
    SHU_CheckPanicNullPointer(thread);

    atomic_store_explicit(&thread->signals, SHUISignal_Destroyed, memory_order_release);

#ifdef _WIN32
    if (!TerminateThread(thread->handle, 0))
    {
        return SHUResult_ErrInternal;
    }

    if (WaitForSingleObject(thread->handle, INFINITE) == WAIT_FAILED)
    {
        return SHUResult_ErrInternal;
    }

    if (!CloseHandle(thread->handle))
    {
        return SHUResult_ErrInternal;
    }
#else
    if (pthread_cancel(thread->handle))
    {
        return SHUResult_ErrInternal;
    }

    if (pthread_join(thread->handle, NULL))
    {
        return SHUResult_ErrInternal;
    }
#endif

    SHU_ThreadClear(thread);
    free(thread);

    return SHUResult_Ok;
}

void SHU_ThreadClear(SHUThread thread)
{
    SHU_CheckPanicNullPointer(thread);

    while (thread->headTask != NULL)
    {
        SHU_TaskDestroy(thread, thread->headTask);
    }
}

SHUTask SHU_TaskGetCurrent(void)
{
    return SHUMUT.currentTask;
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

    SHUTask task = (SHUTask)malloc(sizeof(SHUI_Task) + tempStackSize + 16);
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
        SHUResult result = SHUI_SpawnThreadWithTask(thread, task);
        if (result != SHUResult_Ok)
        {
#ifdef _WIN32
            DeleteFiber(task->context);
#endif
            free(task);
            return result;
        }
    }
    else // append
    {
        SHU_LockWait(thread->taskListLock);
        task->previous = thread->tailTask;
        task->next = thread->headTask;
        thread->tailTask->next = task;
        thread->headTask->previous = task;
        thread->tailTask = task;
        SHU_LockRelease(thread->taskListLock);
    }

    *retTask = task;
    return SHUResult_Ok;
}

void SHU_TaskDestroy(SHUThread thread, SHUTask task)
{
    SHU_CheckPanicNullPointer(task);

    SHUI_TaskUnlink(thread, task);
#ifdef _WIN32
    DeleteFiber(task->context);
#endif
    free(task);
}

void SHU_TaskYield(SHUTask task, u64 milliseconds)
{
    SHU_CheckPanicNullPointer(task);

    if (milliseconds > 0)
    {
        task->wakeAt = SHUI_GetMilliseconds() + (u64)milliseconds;
        task->signals = SHUISignal_Sleeping;
    }
    else
    {
        task->wakeAt = 0;
    }

    SHUI_JumpToContext(&task->context, &SHUMUT.currentThread->context);
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
    return !pthread_mutex_trylock(&lock->mutex);
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

usz SHU_AtomicRead(_Atomic usz *atomicVariable)
{
    return atomic_load_explicit(atomicVariable, memory_order_acquire);
}

void SHU_AtomicWrite(_Atomic usz *atomicVariable, usz writeValue)
{
    atomic_store_explicit(atomicVariable, writeValue, memory_order_release);
}

usz SHU_AtomicAdd(_Atomic usz *atomicVariable, usz sumValue)
{
    if (sumValue == 0)
    {
        return SHU_AtomicRead(atomicVariable);
    }

    return sumValue > 0
               ? atomic_fetch_add_explicit(atomicVariable, sumValue, memory_order_acq_rel)
               : atomic_fetch_sub_explicit(atomicVariable, sumValue, memory_order_acq_rel);
}

#endif // SHU_IMPLEMENTATION

#pragma endregion Definitions
