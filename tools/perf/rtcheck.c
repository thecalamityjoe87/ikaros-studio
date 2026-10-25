/*  rtcheck.so: catches what the audio thread does that a real-time thread
    shouldn't (Linux, GCC or Clang; a stand-in for Clang's RealtimeSanitizer).

    Build:  gcc -O2 -shared -fPIC -o rtcheck.so tools/perf/rtcheck.c -ldl -lpthread
    Use:    IKAROS_PERF=1 LD_PRELOAD=$PWD/rtcheck.so IKAROS_RTCHECK_OUT=rt.txt "Ikaros Studio" ...
    Read:   python3 tools/perf/symbolize.py rt.txt

    PerfMonitor calls ikaros_rtcheck_enter() at the start of each audio
    callback and ikaros_rtcheck_leave() at the end. In between, on that
    thread only, this counts calls to malloc/free & co., mutex/rwlock/condition
    waits, semaphores, sleeps and file I/O, and keeps a stack trace of a
    sample of them. Everything is written out when the app exits.

    How it works: a library named in LD_PRELOAD is loaded before every other,
    so its malloc, pthread_mutex_lock... are the ones the app ends up calling
    (symbol interposition). Each one checks a thread-local flag and then calls
    the real function (glibc's __libc_malloc, or the next definition found
    with dlsym(RTLD_NEXT)).
*/

#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { evMalloc, evFree, evCalloc, evRealloc, evMemalign, evMutex, evRwlock, evCondWait, evSemWait,
       evSleep, evRead, evWrite, evOpen, numEvents };

static const char* eventNames[numEvents] = { "malloc", "free", "calloc", "realloc", "memalign", "mutex_lock",
                                             "rwlock", "cond_wait", "sem_wait", "sleep", "read", "write", "open" };

static __thread int inRealtime;   // 1 between enter and leave, on the audio thread
static __thread int inHook;       // stops our own bookkeeping (backtrace) from counting itself
static __thread int eventsThisCallback;

static atomic_long counts[numEvents];
static atomic_long callbacks, callbacksWithEvents, maxEventsInOne;

// Stack samples: written by the audio thread, read at exit.
enum { maxSamples = 8192, maxFrames = 24 };
typedef struct { int event; int depth; void* frames[maxFrames]; } Sample;
static Sample samples[maxSamples];
static atomic_int numSamples;
static atomic_long eventSerial;

extern void* __libc_malloc (size_t);
extern void  __libc_free (void*);
extern void* __libc_calloc (size_t, size_t);
extern void* __libc_realloc (void*, size_t);
extern void* __libc_memalign (size_t, size_t);

static int (*realMutexLock) (pthread_mutex_t*);
static int (*realRdlock) (pthread_rwlock_t*);
static int (*realWrlock) (pthread_rwlock_t*);
static int (*realCondWait) (pthread_cond_t*, pthread_mutex_t*);
static int (*realCondTimedwait) (pthread_cond_t*, pthread_mutex_t*, const struct timespec*);
static int (*realSemWait) (sem_t*);
static int (*realNanosleep) (const struct timespec*, struct timespec*);
static int (*realUsleep) (useconds_t);
static ssize_t (*realRead) (int, void*, size_t);
static ssize_t (*realWrite) (int, const void*, size_t);
static int (*realOpen) (const char*, int, ...);

static void note (int event)
{
    if (! inRealtime || inHook)
        return;

    inHook = 1;
    atomic_fetch_add_explicit (&counts[event], 1, memory_order_relaxed);
    ++eventsThisCallback;

    // Every event's stack until the buffer is half full, then one in 64.
    const long serial = atomic_fetch_add_explicit (&eventSerial, 1, memory_order_relaxed);
    const int used = atomic_load_explicit (&numSamples, memory_order_relaxed);

    if (used < maxSamples && (used < maxSamples / 2 || serial % 64 == 0))
    {
        const int i = atomic_fetch_add (&numSamples, 1);

        if (i < maxSamples)
        {
            samples[i].event = event;
            samples[i].depth = backtrace (samples[i].frames, maxFrames);
        }
    }

    inHook = 0;
}

void ikaros_rtcheck_enter (void)
{
    inRealtime = 1;
    eventsThisCallback = 0;
}

void ikaros_rtcheck_leave (void)
{
    inRealtime = 0;
    atomic_fetch_add_explicit (&callbacks, 1, memory_order_relaxed);

    if (eventsThisCallback > 0)
    {
        atomic_fetch_add_explicit (&callbacksWithEvents, 1, memory_order_relaxed);

        long previous = atomic_load (&maxEventsInOne);

        while (eventsThisCallback > previous
                && ! atomic_compare_exchange_weak (&maxEventsInOne, &previous, eventsThisCallback))
        {
        }
    }
}

//==============================================================================
void* malloc (size_t n)                    { note (evMalloc); return __libc_malloc (n); }
void  free (void* p)                       { if (p != NULL) note (evFree); __libc_free (p); }
void* calloc (size_t a, size_t b)          { note (evCalloc); return __libc_calloc (a, b); }
void* realloc (void* p, size_t n)          { note (evRealloc); return __libc_realloc (p, n); }
void* memalign (size_t a, size_t n)        { note (evMemalign); return __libc_memalign (a, n); }
void* aligned_alloc (size_t a, size_t n)   { note (evMemalign); return __libc_memalign (a, n); }

int posix_memalign (void** out, size_t a, size_t n)
{
    note (evMemalign);
    *out = __libc_memalign (a, n);
    return *out != NULL ? 0 : 12;   // ENOMEM
}

// Another library's constructor can call these before ours has run, so each
// finds the real function itself the first time it's needed.
static void* resolve (void** slot, const char* name)
{
    if (*slot == NULL)
        *slot = dlsym (RTLD_NEXT, name);

    return *slot;
}

#define REAL(fn, name) ((__typeof__ (fn)) resolve ((void**) &(fn), name))

int pthread_mutex_lock (pthread_mutex_t* m)            { note (evMutex); return REAL (realMutexLock, "pthread_mutex_lock") (m); }
int pthread_rwlock_rdlock (pthread_rwlock_t* l)        { note (evRwlock); return REAL (realRdlock, "pthread_rwlock_rdlock") (l); }
int pthread_rwlock_wrlock (pthread_rwlock_t* l)        { note (evRwlock); return REAL (realWrlock, "pthread_rwlock_wrlock") (l); }
int pthread_cond_wait (pthread_cond_t* c, pthread_mutex_t* m)
                                                       { note (evCondWait); return REAL (realCondWait, "pthread_cond_wait") (c, m); }
int pthread_cond_timedwait (pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* t)
                                                       { note (evCondWait); return REAL (realCondTimedwait, "pthread_cond_timedwait") (c, m, t); }
int sem_wait (sem_t* s)                                { note (evSemWait); return REAL (realSemWait, "sem_wait") (s); }
int nanosleep (const struct timespec* a, struct timespec* b)
                                                       { note (evSleep); return REAL (realNanosleep, "nanosleep") (a, b); }
int usleep (useconds_t u)                              { note (evSleep); return REAL (realUsleep, "usleep") (u); }
ssize_t read (int fd, void* b, size_t n)               { note (evRead); return REAL (realRead, "read") (fd, b, n); }
ssize_t write (int fd, const void* b, size_t n)        { note (evWrite); return REAL (realWrite, "write") (fd, b, n); }

int open (const char* path, int flags, ...)
{
    // The mode argument is only there when a file may be created.
    mode_t mode = 0;

    if ((flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE)
    {
        va_list args;
        va_start (args, flags);
        mode = (mode_t) va_arg (args, int);
        va_end (args);
    }

    note (evOpen);
    return REAL (realOpen, "open") (path, flags, mode);
}

//==============================================================================
__attribute__ ((constructor)) static void setUp (void)
{
    realMutexLock = dlsym (RTLD_NEXT, "pthread_mutex_lock");
    realRdlock = dlsym (RTLD_NEXT, "pthread_rwlock_rdlock");
    realWrlock = dlsym (RTLD_NEXT, "pthread_rwlock_wrlock");
    realCondWait = dlvsym (RTLD_NEXT, "pthread_cond_wait", "GLIBC_2.3.2");
    realCondTimedwait = dlvsym (RTLD_NEXT, "pthread_cond_timedwait", "GLIBC_2.3.2");
    realSemWait = dlsym (RTLD_NEXT, "sem_wait");
    realNanosleep = dlsym (RTLD_NEXT, "nanosleep");
    realUsleep = dlsym (RTLD_NEXT, "usleep");
    realRead = dlsym (RTLD_NEXT, "read");
    realWrite = dlsym (RTLD_NEXT, "write");
    realOpen = dlsym (RTLD_NEXT, "open");

    if (realCondWait == NULL)
        realCondWait = dlsym (RTLD_NEXT, "pthread_cond_wait");

    if (realCondTimedwait == NULL)
        realCondTimedwait = dlsym (RTLD_NEXT, "pthread_cond_timedwait");

    // backtrace() loads libgcc the first time it's used, which allocates:
    // get that out of the way now.
    void* frames[4];
    backtrace (frames, 4);
}

__attribute__ ((destructor)) static void writeReport (void)
{
    inRealtime = 0;
    inHook = 1;

    // LD_PRELOAD also reaches other programs (xvfb-run, Xvfb): only the app,
    // which has had audio callbacks, writes a report.
    if (atomic_load (&callbacks) == 0)
        return;

    const char* path = getenv ("IKAROS_RTCHECK_OUT");
    char fallback[64];

    if (path == NULL)
    {
        snprintf (fallback, sizeof (fallback), "/tmp/ikaros-rtcheck-%d.txt", (int) getpid());
        path = fallback;
    }

    FILE* f = fopen (path, "w");

    if (f == NULL)
        return;

    fprintf (f, "callbacks %ld\ncallbacks_with_events %ld\nmax_events_in_one_callback %ld\n",
             atomic_load (&callbacks), atomic_load (&callbacksWithEvents), atomic_load (&maxEventsInOne));

    for (int e = 0; e < numEvents; ++e)
        fprintf (f, "count %s %ld\n", eventNames[e], atomic_load (&counts[e]));

    // The memory map, so symbolize.py can turn addresses into function names.
    FILE* maps = fopen ("/proc/self/maps", "r");
    char line[1024];

    while (maps != NULL && fgets (line, sizeof (line), maps) != NULL)
        fprintf (f, "map %s", line);

    if (maps != NULL)
        fclose (maps);

    const int n = atomic_load (&numSamples) < maxSamples ? atomic_load (&numSamples) : maxSamples;

    for (int i = 0; i < n; ++i)
    {
        fprintf (f, "stack %s", eventNames[samples[i].event]);

        // Frame 0 is note(), frame 1 our hook: skip them.
        for (int d = 2; d < samples[i].depth; ++d)
            fprintf (f, " %p", samples[i].frames[d]);

        fprintf (f, "\n");
    }

    fclose (f);
}
