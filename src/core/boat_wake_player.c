#include "boat_wake_player.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct BoatWakePlayer {
    BoatWake *field;
    double *impulse;
    BoatWakeConfig config;
    BoatWakePlayerReport report;
};

BoatWakePlayer *boat_wake_player_create(double dt)
{
    BoatWakePlayer *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    w->config
        = (BoatWakeConfig) { 384, 256, .75, dt, BOAT_WAKE_DENSITY, 256, NULL, NULL, 8, false };
    size_t count = w->config.width * w->config.height;
    w->impulse = calloc(count, sizeof(double));
    double *rates = calloc(count, sizeof(double));
    if (!rates || !w->impulse) {
        free(rates);
        boat_wake_player_destroy(w);
        return NULL;
    }
    for (size_t z = 0; z < w->config.height; ++z)
        for (size_t x = 0; x < w->config.width; ++x) {
            double px = ((double)x + .5) * .75 - 144, pz = ((double)z + .5) * .75 - 96;
            /* Begin absorption 10.5m before the visible absorbing basin bank.
             * Continue through the exterior; never clip or scroll live state. */
            double edge = fmin(120 - fabs(px), 80 - fabs(pz));
            double t = fmax(0, fmin(1, (10.5 - edge) / 10.5));
            rates[z * w->config.width + x] = 2 * t * t;
        }
    w->config.absorption = rates;
    w->field = boat_wake_create(&w->config);
    free(rates);
    w->config.absorption = NULL;
    if (!w->field || !boat_wake_use_cosine(w->field)) {
        boat_wake_player_destroy(w);
        return NULL;
    }
    boat_wake_prepare(w->field);
    return w;
}

void boat_wake_player_destroy(BoatWakePlayer *w)
{
    if (w) {
        boat_wake_destroy(w->field);
        free(w->impulse);
        free(w);
    }
}

void boat_wake_player_reset(BoatWakePlayer *w)
{
    if (!w)
        return;
    boat_wake_reset(w->field); /* Audited retirement, not absorption. */
    boat_wake_prepare(w->field);
}

const BoatWake *boat_wake_player_field(const BoatWakePlayer *w) { return w ? w->field : NULL; }

static bool valid_stern(BoatWakeStern s)
{
    return isfinite(s.x) && isfinite(s.z) && isfinite(s.vx) && isfinite(s.vz) && isfinite(s.wet)
        && s.wet >= 0 && s.wet <= 1 && isfinite(s.resistance) && s.resistance >= 0
        && isfinite(s.drag_power) && s.drag_power >= 0 && fabs(s.x) < 10000 && fabs(s.z) < 10000
        && fabs(s.vx) < 1000 && fabs(s.vz) < 1000 && s.resistance < 1000 && s.drag_power < 1e6;
}

bool boat_wake_player_step(BoatWakePlayer *w, BoatWakeStern a, BoatWakeStern b)
{
    if (!w || !valid_stern(a) || !valid_stern(b))
        return false;
    clock_t begin = clock();
    BoatWakeConfig c = w->config;
    memset(w->impulse, 0, c.width * c.height * sizeof(double));
    double px = (a.x + b.x) * .5, pz = (a.z + b.z) * .5;
    double vx = (a.vx + b.vx) * .5, vz = (a.vz + b.vz) * .5, speed = hypot(vx, vz);
    double wet = .5 * (a.wet + b.wet), resistance = .5 * (a.resistance + b.resistance);
    double available = BOAT_WAKE_MASS * .5 * (a.drag_power + b.drag_power) * wet * c.dt;
    /* A moving, balanced pressure dipole. psi_t=-pressure/rho. Width 1.5m
     * resolves the useful 3..12m band; no fixed V angle or oscillator phase.
     * Pressure is proportional to the existing resistance force / footprint area.
     * The speed-dependent gain is an explicit source calibration, not an assertion that wave
     * work equals drag work. Measure the signed Hamiltonian work independently. */
    const double sigma = 1.5, radius = 7.5;
    /* Gameplay calibration for the declared 2t effective hull. Ease in the
     * high-speed pressure late, with zero derivative at both endpoints, so the
     * larger full-speed ramp still fits the measured resistance-work budget
     * through displacement, transition and planing speeds. */
    double planing = fmax(0, fmin(1, (speed - 6) / 16));
    double rise = planing * planing * planing * planing * (5 - 4 * planing);
    double gain = 8 * fmin(speed / 3, 4) * (1 + 2 * rise);
    double strength = gain * BOAT_WAKE_MASS * resistance * wet
        / (BOAT_WAKE_DENSITY * 2 * 3.141592653589793 * sigma * sigma);
    if (speed > 1e-8 && strength > 0) {
        int x0 = (int)fmax(0, floor((px - radius) / c.spacing + c.width * .5));
        int z0 = (int)fmax(0, floor((pz - radius) / c.spacing + c.height * .5));
        int x1 = (int)fmin(c.width - 1, ceil((px + radius) / c.spacing + c.width * .5));
        int z1 = (int)fmin(c.height - 1, ceil((pz + radius) / c.spacing + c.height * .5));
        double sum = 0, weight = 0;
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                double dx = (x + .5 - c.width * .5) * c.spacing - px;
                double dz = (z + .5 - c.height * .5) * c.spacing - pz;
                double r2 = (dx * dx + dz * dz) / (sigma * sigma);
                double envelope = exp(-.5 * r2);
                double v = (dx * vx + dz * vz) / (sigma * speed) * envelope;
                w->impulse[(size_t)z * c.width + (size_t)x] = v;
                sum += v;
                weight += envelope;
            }
        /* Remove quadrature/truncation mean locally; never create a global source. */
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                double dx = (x + .5 - c.width * .5) * c.spacing - px;
                double dz = (z + .5 - c.height * .5) * c.spacing - pz;
                double envelope = exp(-.5 * (dx * dx + dz * dz) / (sigma * sigma));
                size_t i = (size_t)z * c.width + (size_t)x;
                w->impulse[i] = -strength * c.dt * (w->impulse[i] - sum / weight * envelope);
            }
    }
    clock_t sourced = clock();
    if (!boat_wake_step_with_impulse(w->field, w->impulse))
        return false;
    clock_t stepped = clock();
    boat_wake_prepare(w->field);
    w->report.energy = boat_wake_ledger(w->field);
    w->report.resistance_work += available;
    w->report.source_seconds += (double)(sourced - begin) / CLOCKS_PER_SEC;
    w->report.update_seconds += (double)(stepped - sourced) / CLOCKS_PER_SEC;
    w->report.query_audit_seconds += (double)(clock() - stepped) / CLOCKS_PER_SEC;
    ++w->report.steps;
    return true;
}

BoatWakePlayerReport boat_wake_player_report(BoatWakePlayer *w)
{
    if (!w)
        return (BoatWakePlayerReport) { 0 };
    w->report.energy = boat_wake_ledger(w->field);
    boat_wake_extrema(w->field, &w->report.peak_height, &w->report.peak_slope);
    return w->report;
}

size_t boat_wake_player_storage(const BoatWakePlayer *w)
{
    return w ? sizeof(*w) + w->config.width * w->config.height * sizeof(double)
            + boat_wake_storage_bytes(w->field)
             : 0;
}
