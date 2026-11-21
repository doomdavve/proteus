#ifndef PROTEUS_WAKE_TRANSPORT_H
#define PROTEUS_WAKE_TRANSPORT_H

#include "wake_capture.h"

// Private p=1 experiment: epsilon=.25, frozen successor-only K, baseline cadence/lifetime.
BoatWakeObjectSample r2_transport_piece(
    const BoatWakeObject *record, float curvature, Vector2 point, double seconds);
BoatWakeObjectSample r2_transport_query(const R2Capture *capture, Vector2 point, double seconds);
// Conservative union bounds for baseline AND widened support at this time, not camera bounds.
void r2_transport_bounds(const R2Capture *capture, double seconds, Vector2 *low, Vector2 *high);

#endif
