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
void thread_event_send(thread_ev event);
void thread_event_wait(thread_ev event);

#if defined(_WIN32) || defined(__WIN32__)
//////////////////////////
// Windows Implementation
//////////////////////////

thread_ev thread_event_create(void)
{
    return CreateEventW(NULL, false, false, NULL);
}

void thread_event_send(thread_ev event)
{
    SetEvent(event);
}

void thread_event_wait(thread_ev event)
{
    WaitForSingleObject(event, INFINITE);
}


#else
////////////////////////
// POSIX Implementation
////////////////////////

thread_ev thread_event_create(void)
{
    return eventfd(0, 0);
}

void thread_event_send(thread_ev event)
{
    u64 buf = 1;
    write(event, &buf, sizeof(buf));
}

void thread_event_wait(thread_ev event)
{
    struct epoll_event epoll_ev;
    epoll_wait(event, &epoll_ev, 1, -1);
}


#endif
