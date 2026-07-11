# Shumut
Shumut (SHU MUlti Threading/Tasking) is a portable, cross platform multi threading and multitasking library.

It uses the [SHU](https://github.com/omerfuyar/shu) system. By defining `SHU`, you can tell the library where to find `shu.h`.

Goal is to have an easy way of multithreading and concurrency (like coroutines, green threads) in C.

On POSIX systems, you most likely will need to link with pthreads (`-pthread` flag). On modern systems, libc already includes pthreads inside.