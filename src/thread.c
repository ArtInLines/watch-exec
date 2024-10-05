#include "header.h"

typedef thrd_t thread;
#if defined(_WIN32) || defined(__WIN32__)
#   include <windows.h>
    typedef HANDLE thread_ev;
#else
#   include <sys/eventfd.h>
#   include <sys/epoll.h>
#   include <unistd.h>
    typedef int thread_ev;
#endif

// Forward declarations of functions, that all platforms need to implement
thread_ev thread_event_create(void);
b32 thread_event_close(thread_ev event);
b32 thread_event_send(thread_ev event);
b32 thread_event_wait(thread_ev event);

#if defined(_WIN32) || defined(__WIN32__)
//////////////////////////
// Windows Implementation
//////////////////////////

thread_ev thread_event_create(void)
{
    return CreateEventW(NULL, false, false, NULL);
}

b32 thread_event_close(thread_ev event)
{
    return CloseHandle(event);
}

b32 thread_event_send(thread_ev event)
{
    return SetEvent(event);
}

b32 thread_event_wait(thread_ev event)
{
    return WaitForSingleObject(event, INFINITE);
}


#else
////////////////////////
// POSIX Implementation
////////////////////////

thread_ev thread_event_create(void)
{
    return eventfd(0, 0);
}

b32 thread_event_close(thread_ev event)
{
    return close(event) == 0;
}

b32 thread_event_send(thread_ev event)
{
    u64 buf = 1;
    ssize_t written_count = write(event, &buf, sizeof(buf));
    return written_count >= 0;
}

b32 thread_event_wait(thread_ev event)
{
    u64 buf;
    ssize_t read_count = read(event, &buf, sizeof(buf));
    return read_count >= 0;
}


#endif
