// Absolute-deadline frame pacing for windowed loops.
//
// raylib's SetTargetFPS() measures each wait from the end of the previous frame, so every
// oversleep is lost; macOS usleep() oversleeps by more than raylib's busy-wait reserve and the
// game settled near 58.6 Hz. The pacer keeps a chain of deadlines instead and repays a late frame
// from the next one. Use it with SetTargetFPS(0) and call frame_pacer_wait() just before
// EndDrawing(): the wait then precedes the swap and input is still polled right after it.
#pragma once

#include <stdbool.h>

#define FRAME_PACER_HZ 60
// With vsync the swap paces the loop. The backstop runs this much faster than the display so it
// never holds back a swap that vsync would present on time, yet still caps hidden or occluded
// windows whose swaps stop blocking.
#define FRAME_PACER_VSYNC_HEADROOM 1.1
// Sleep stops this far before a deadline and the rest is spun, absorbing typical oversleep.
#define FRAME_PACER_SPIN_SECONDS 0.001

typedef struct {
    double period; // Seconds per frame; 0 disables pacing.
    double deadline; // GetTime() of the current frame boundary.
    bool started;
} FramePacer;

void frame_pacer_init(FramePacer *pacer, double hz);
// Configures the pacer for the current window and returns its rate in Hz. With FLAG_VSYNC_HINT the
// rate is the backstop above the current monitor's refresh (60 Hz assumed when unknown); without
// it, FRAME_PACER_HZ. Web builds leave pacing to the browser and return 0.
double frame_pacer_init_window(FramePacer *pacer);
// Advances the deadline chain at time NOW and returns the absolute time to wait until, which is NOW
// when the frame is late. A frame more than one period behind restarts the chain at NOW rather than
// being repaid with a burst of short frames.
double frame_pacer_next(FramePacer *pacer, double now);
void frame_pacer_wait(FramePacer *pacer);
