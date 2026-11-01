#ifndef PROTEUS_BOAT_GPU_TIMER_H
#define PROTEUS_BOAT_GPU_TIMER_H

// Asynchronous GL_TIME_ELAPSED sampling shared by the benchmark and live capture. A busy slot
// skips its frame instead of waiting; results are linked to the caller's frame identity and are
// only read after the query reports availability. Include after a raylib/rlgl header that
// exposes the GL query entry points. WebGL2 has no GL_TIME_ELAPSED, so the browser build compiles
// an always-unsupported timer: its samples are missing rather than zero.
#include <limits.h>
#include <stdbool.h>

#define BOAT_GPU_TIMER_SLOTS 8

typedef void (*BoatGpuTimerComplete)(void *context, unsigned long long frame, double milliseconds);

#if defined(__EMSCRIPTEN__)

typedef struct {
    bool supported;
} BoatGpuTimer;

static inline void boat_gpu_timer_init(BoatGpuTimer *timer) { timer->supported = false; }

static inline bool boat_gpu_timer_begin(BoatGpuTimer *timer, unsigned long long frame)
{
    (void)timer;
    (void)frame;
    return false;
}

static inline void boat_gpu_timer_end(void) { }

static inline void boat_gpu_timer_poll(
    BoatGpuTimer *timer, BoatGpuTimerComplete complete, void *context, bool wait)
{
    (void)timer;
    (void)complete;
    (void)context;
    (void)wait;
}

static inline void boat_gpu_timer_destroy(BoatGpuTimer *timer) { (void)timer; }

#else

typedef struct {
    GLuint ids[BOAT_GPU_TIMER_SLOTS];
    unsigned long long frame[BOAT_GPU_TIMER_SLOTS];
    bool supported;
} BoatGpuTimer;

static inline void boat_gpu_timer_init(BoatGpuTimer *timer)
{
    *timer = (BoatGpuTimer) { .supported = false };
    for (int i = 0; i < BOAT_GPU_TIMER_SLOTS; ++i)
        timer->frame[i] = ULLONG_MAX;
    if (!glGenQueries || !glBeginQuery || !glEndQuery || !glGetQueryiv || !glGetQueryObjectiv
        || !glGetQueryObjectui64v)
        return;
    while (glGetError() != GL_NO_ERROR) { }
    GLint bits = 0;
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
    if (bits <= 0)
        return;
    glGenQueries(BOAT_GPU_TIMER_SLOTS, timer->ids);
    timer->supported = glGetError() == GL_NO_ERROR;
}

static inline bool boat_gpu_timer_begin(BoatGpuTimer *timer, unsigned long long frame)
{
    int slot = (int)(frame % BOAT_GPU_TIMER_SLOTS);
    if (!timer->supported || timer->frame[slot] != ULLONG_MAX)
        return false;
    glBeginQuery(GL_TIME_ELAPSED, timer->ids[slot]);
    timer->frame[slot] = frame;
    return true;
}

static inline void boat_gpu_timer_end(void) { glEndQuery(GL_TIME_ELAPSED); }

// WAIT is used only after measurement has finished. Unsent or unavailable results stay pending.
static inline void boat_gpu_timer_poll(
    BoatGpuTimer *timer, BoatGpuTimerComplete complete, void *context, bool wait)
{
    for (int i = 0; timer->supported && i < BOAT_GPU_TIMER_SLOTS; ++i) {
        if (timer->frame[i] == ULLONG_MAX)
            continue;
        GLint ready = wait;
        if (!wait)
            glGetQueryObjectiv(timer->ids[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready)
            continue;
        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(timer->ids[i], GL_QUERY_RESULT, &elapsed);
        complete(context, timer->frame[i], (double)elapsed / 1e6);
        timer->frame[i] = ULLONG_MAX;
    }
}

static inline void boat_gpu_timer_destroy(BoatGpuTimer *timer)
{
    if (timer->supported)
        glDeleteQueries(BOAT_GPU_TIMER_SLOTS, timer->ids);
    *timer = (BoatGpuTimer) { 0 };
}

#endif

#endif
