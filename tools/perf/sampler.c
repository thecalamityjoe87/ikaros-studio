/*  sampler.so: a sampling CPU profiler for any thread of the app (Linux).
    For when `perf` isn't allowed (kernel.perf_event_paranoid > 2) and gdb
    can't attach.

    Build:  gcc -O2 -shared -fPIC -o sampler.so tools/perf/sampler.c -ldl -lpthread -lrt
    Use:    LD_PRELOAD=$PWD/sampler.so IKAROS_SAMPLER_OUT=prof.txt "Ikaros Studio" ...
    Read:   python3 tools/perf/symbolize.py prof.txt

    Every thread gets a timer that runs on that thread's own CPU time (like
    perf's task-clock): after each IKAROS_SAMPLER_US microseconds (default
    1000) of CPU the thread has used, the kernel interrupts it with SIGPROF
    and the handler records its stack. So a thread that's busy 50% of the time
    gets half as many samples as one that's busy all the time, and idle
    threads get none. Samples are written out when the app exits.
*/

#define _GNU_SOURCE
#include <dirent.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

enum { maxSamples = 400000, maxFrames = 32, maxThreads = 1024 };

typedef struct { int tid; int depth; void* frames[maxFrames]; } Sample;

static Sample* samples;
static atomic_int numSamples;
static atomic_int stopping;

static struct { int tid; timer_t timer; } threads[maxThreads];
static int numThreads;
static pthread_t watcher;
static long intervalNs = 1000000;

static void onSample (int sig, siginfo_t* info, void* context)
{
    (void) sig; (void) info; (void) context;

    if (atomic_load_explicit (&stopping, memory_order_relaxed))
        return;

    const int i = atomic_fetch_add_explicit (&numSamples, 1, memory_order_relaxed);

    if (i >= maxSamples)
        return;

    samples[i].tid = (int) syscall (SYS_gettid);
    samples[i].depth = backtrace (samples[i].frames, maxFrames);
}

// The kernel's clock ID for another thread's CPU time (MAKE_THREAD_CPUCLOCK
// in the kernel's headers: CPUCLOCK_SCHED | CPUCLOCK_PERTHREAD_MASK).
static clockid_t threadCpuClock (int tid)
{
    return (clockid_t) ((~(unsigned) tid << 3) | 6);
}

static void watchThread (int tid)
{
    for (int i = 0; i < numThreads; ++i)
        if (threads[i].tid == tid)
            return;

    if (numThreads >= maxThreads)
        return;

    struct sigevent event;
    memset (&event, 0, sizeof (event));
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_signo = SIGPROF;
    event._sigev_un._tid = tid;   // glibc has no friendlier name for it

    timer_t timer;

    if (timer_create (threadCpuClock (tid), &event, &timer) != 0)
        return;

    struct itimerspec spec = { { 0, intervalNs }, { 0, intervalNs } };
    timer_settime (timer, 0, &spec, NULL);

    threads[numThreads].tid = tid;
    threads[numThreads].timer = timer;
    ++numThreads;
}

// Threads come and go: look for new ones five times a second.
static void* watch (void* unused)
{
    (void) unused;
    const int self = (int) syscall (SYS_gettid);

    while (! atomic_load (&stopping))
    {
        DIR* dir = opendir ("/proc/self/task");

        if (dir != NULL)
        {
            struct dirent* entry;

            while ((entry = readdir (dir)) != NULL)
            {
                const int tid = atoi (entry->d_name);

                if (tid > 0 && tid != self)
                    watchThread (tid);
            }

            closedir (dir);
        }

        usleep (200000);
    }

    return NULL;
}

static int active;   // only in the app (LD_PRELOAD also reaches xvfb-run, Xvfb...)

__attribute__ ((constructor)) static void start (void)
{
    // IKAROS_SAMPLER_COMM picks the process by its name (default: the app's).
    char comm[64] = "";
    FILE* f = fopen ("/proc/self/comm", "r");

    if (f != NULL)
    {
        if (fgets (comm, sizeof (comm), f) != NULL)
            comm[strcspn (comm, "\n")] = 0;

        fclose (f);
    }

    const char* wanted = getenv ("IKAROS_SAMPLER_COMM") != NULL ? getenv ("IKAROS_SAMPLER_COMM") : "Ikaros Studio";

    if (strcmp (comm, wanted) != 0)
        return;

    active = 1;

    if (getenv ("IKAROS_SAMPLER_US") != NULL)
        intervalNs = atol (getenv ("IKAROS_SAMPLER_US")) * 1000;

    samples = calloc (maxSamples, sizeof (Sample));

    // backtrace() loads libgcc the first time; do it now, not in the handler.
    void* frames[4];
    backtrace (frames, 4);

    struct sigaction action;
    memset (&action, 0, sizeof (action));
    action.sa_sigaction = onSample;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset (&action.sa_mask);
    sigaction (SIGPROF, &action, NULL);

    pthread_create (&watcher, NULL, watch, NULL);
}

__attribute__ ((destructor)) static void finish (void)
{
    if (! active)
        return;

    atomic_store (&stopping, 1);

    for (int i = 0; i < numThreads; ++i)
        timer_delete (threads[i].timer);

    const char* path = getenv ("IKAROS_SAMPLER_OUT");
    char fallback[64];

    if (path == NULL)
    {
        snprintf (fallback, sizeof (fallback), "/tmp/ikaros-sampler-%d.txt", (int) getpid());
        path = fallback;
    }

    FILE* f = fopen (path, "w");

    if (f == NULL)
        return;

    fprintf (f, "interval_us %ld\n", intervalNs / 1000);

    FILE* maps = fopen ("/proc/self/maps", "r");
    char line[1024];

    while (maps != NULL && fgets (line, sizeof (line), maps) != NULL)
        fprintf (f, "map %s", line);

    if (maps != NULL)
        fclose (maps);

    // Thread names (the ones still running; others show as their ID).
    for (int i = 0; i < numThreads; ++i)
    {
        char commPath[64], name[64] = "";
        snprintf (commPath, sizeof (commPath), "/proc/self/task/%d/comm", threads[i].tid);
        FILE* comm = fopen (commPath, "r");

        if (comm != NULL && fgets (name, sizeof (name), comm) != NULL)
            fprintf (f, "thread %d %s", threads[i].tid, name);

        if (comm != NULL)
            fclose (comm);
    }

    const int n = atomic_load (&numSamples) < maxSamples ? atomic_load (&numSamples) : maxSamples;

    for (int i = 0; i < n; ++i)
    {
        fprintf (f, "sample %d", samples[i].tid);

        // Frame 0 is the handler, 1 the signal trampoline.
        for (int d = 2; d < samples[i].depth; ++d)
            fprintf (f, " %p", samples[i].frames[d]);

        fprintf (f, "\n");
    }

    fclose (f);
}
