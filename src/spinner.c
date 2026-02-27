#include "spinner.h"

#include <pthread.h>
#include <stdio.h>
#include <time.h>

/* Breathing dot using the terminal's system cyan (16-color palette) so it
   respects the user's color theme.  Inhale (3 frames) is faster than
   exhale (5 frames), and a long rest at the minimum creates the organic
   pause between breaths.  120 ms ticks give a ~2 s cycle. */
static const char* const s_frames[] = {
    "\033[2;36m·\033[0m",   /* rest     */
    "\033[2;36m·\033[0m",   /* rest     */
    "\033[2;36m·\033[0m",   /* rest     */
    "\033[2;36m·\033[0m",   /* rest     */
    "\033[2;36m·\033[0m",   /* rest     */
    "\033[36m·\033[0m",     /* inhale ↑ */
    "\033[36m•\033[0m",     /* inhale ↑ */
    "\033[1;36m●\033[0m",   /* peak     */
    "\033[1;36m•\033[0m",   /* exhale ↓ */
    "\033[1;36m•\033[0m",   /* exhale ↓ */
    "\033[36m•\033[0m",     /* exhale ↓ */
    "\033[36m·\033[0m",     /* exhale ↓ */
    "\033[2;36m·\033[0m",   /* exhale ↓ */
    "\033[2;36m·\033[0m",   /* settling */
};
static const int s_nframes = 14;

static pthread_mutex_t s_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_cond    = PTHREAD_COND_INITIALIZER;
static volatile int    s_active  = 0; /* thread loop condition */
static volatile int    s_enabled = 0; /* whether to draw frames */
static int             s_started = 0; /* thread exists */
static pthread_t       s_thread;

static void clear_frame(void)
{
    /* Restore saved cursor, overwrite frame char with space, restore again. */
    fprintf(stderr, "\033[u \033[u\033[?25h");
    fflush(stderr);
}

static void save_cursor(void)
{
    fprintf(stderr, "\033[s\033[?25l");
    fflush(stderr);
}

static void* thread_fn(void* arg)
{
    (void)arg;
    int frame = 0;

    while (1)
    {
        pthread_mutex_lock(&s_mutex);

        if (!s_active)
        {
            if (s_enabled)
                clear_frame();
            pthread_mutex_unlock(&s_mutex);
            break;
        }

        if (s_enabled)
        {
            fprintf(stderr, "\033[u%s\033[u", s_frames[frame % s_nframes]);
            fflush(stderr);
            frame++;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 120000000L; /* 120 ms */
        if (ts.tv_nsec >= 1000000000L)
        {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&s_cond, &s_mutex, &ts);
        pthread_mutex_unlock(&s_mutex);
    }

    return NULL;
}

void spinner_start(void)
{
    if (s_started)
        return;
    s_active  = 1;
    s_enabled = 0;
    s_started = 1;
    pthread_create(&s_thread, NULL, thread_fn, NULL);
}

void spinner_stop(void)
{
    if (!s_started)
        return;
    s_started = 0;
    pthread_mutex_lock(&s_mutex);
    s_active = 0;
    pthread_cond_signal(&s_cond);
    pthread_mutex_unlock(&s_mutex);
    pthread_join(s_thread, NULL);
}

void spinner_turn_start(void)
{
    if (!s_started)
        return;
    pthread_mutex_lock(&s_mutex);
    save_cursor();
    s_enabled = 1;
    pthread_cond_signal(&s_cond);
    pthread_mutex_unlock(&s_mutex);
}

void spinner_turn_end(void)
{
    if (!s_started)
        return;
    pthread_mutex_lock(&s_mutex);
    if (s_enabled)
    {
        clear_frame();
        s_enabled = 0;
    }
    pthread_mutex_unlock(&s_mutex);
}

void spinner_write_chunk(const char* text)
{
    if (!s_started)
    {
        fputs(text, stdout);
        fflush(stdout);
        return;
    }

    pthread_mutex_lock(&s_mutex);
    int was_enabled = s_enabled;
    if (was_enabled)
    {
        clear_frame();
        s_enabled = 0;
    }
    fputs(text, stdout);
    fflush(stdout);
    if (was_enabled)
    {
        save_cursor();
        s_enabled = 1;
        pthread_cond_signal(&s_cond);
    }
    pthread_mutex_unlock(&s_mutex);
}
