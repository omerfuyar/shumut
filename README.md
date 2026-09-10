# Shumut
Shumut (SHU MUlti Threading/Tasking) is a portable, cross platform multi threading and multitasking library.

It uses the [SHU](https://github.com/omerfuyar/shu) system. By defining `SHU` you can tell the library where to find `shu.h` or include it yourself  before any shu... library to prevent any complication. See [SHU](https://github.com/omerfuyar/shu) repo for more information.

Goal is to have an easy way of multithreading and concurrency (like coroutines, green threads) in C. See examples to learn how to use.

On POSIX systems, you most likely will need to link with pthreads (`-pthread` flag). On modern systems, libc already includes pthreads inside.