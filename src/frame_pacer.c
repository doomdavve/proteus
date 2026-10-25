#include "frame_pacer.h"

#include "raylib.h"

void frame_pacer_init(FramePacer *pacer, double hz)
{
    *pacer = (FramePacer) { .period = hz > 0 ? 1.0 / hz : 0 };
}

double frame_pacer_init_window(FramePacer *pacer)
{
#if defined(__EMSCRIPTEN__)
    double hz = 0;
#else
    double hz = FRAME_PACER_HZ;
    if (IsWindowState(FLAG_VSYNC_HINT)) {
        int refresh = GetMonitorRefreshRate(GetCurrentMonitor());
        hz = (refresh > 0 ? refresh : FRAME_PACER_HZ) * FRAME_PACER_VSYNC_HEADROOM;
    }
#endif
    frame_pacer_init(pacer, hz);
    return hz;
}

double frame_pacer_next(FramePacer *pacer, double now)
{
    if (pacer->period <= 0)
        return now;
    double target = pacer->deadline + pacer->period;
    if (!pacer->started || now > target + pacer->period) {
        pacer->started = true;
        pacer->deadline = now;
        return now;
    }
    pacer->deadline = target;
    return now < target ? target : now;
}

void frame_pacer_wait(FramePacer *pacer)
{
    double target = frame_pacer_next(pacer, GetTime());
    double remaining = target - GetTime();
    if (remaining > FRAME_PACER_SPIN_SECONDS)
        WaitTime(remaining - FRAME_PACER_SPIN_SECONDS);
    while (GetTime() < target) { }
}
