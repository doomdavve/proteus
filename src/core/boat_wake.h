#ifndef PROTEUS_BOAT_WAKE_H
#define PROTEUS_BOAT_WAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Owned wave field. Never copy by value; clone creates an independent snapshot.
 * Units: metres, seconds, density kg/m^3, energy joules. */
typedef struct BoatWake BoatWake;
typedef struct {
    size_t width, height;
    double spacing, dt, density;
    unsigned order; /* Chebyshev square-root order; controls long-wave approximation. */
    const unsigned char *land; /* Optional row-major mask, copied at creation. */
    const double *absorption; /* Optional eta damping rates in 1/s, copied. */
    unsigned spatial_order; /* 0/2: cardinal graph; 4/8: powers of water-only axis graphs. */
    bool reference_operator; /* Diagnostic scalar oracle; copied with snapshots. */
} BoatWakeConfig;
typedef struct {
    double injected, positive_work, negative_work;
    double absorbed, retired, numerical_residual, retained;
    uint64_t tick;
} BoatWakeLedger;

BoatWake *boat_wake_create(const BoatWakeConfig *config);
BoatWake *boat_wake_clone(const BoatWake *field);
void boat_wake_destroy(BoatWake *field);
/* Retires all remaining energy explicitly; preserves cumulative ledger, restarts tick. */
void boat_wake_reset(BoatWake *field);
bool boat_wake_step(BoatWake *field);
/* One fixed step with a prescribed interval impulse split symmetrically around it.
 * Reuses source work (or cosine endpoint products); callers supply midpoint quadrature of the
 * interval's total source. Each half's signed work is accounted separately.
 * Invalid impulses are rejected before mutation, as with boat_wake_kick(). */
bool boat_wake_step_with_impulse(BoatWake *field, const double *potential_impulse);
/* Add prescribed potential impulse (m^2/s). Signed Hamiltonian change is source work.
 * Sum simultaneous emitters into one array to pay for one operator. The ledger then
 * classifies the net event's signed work, including interference, not each emitter. */
bool boat_wake_kick(BoatWake *field, const double *potential_impulse);
bool boat_wake_sample(const BoatWake *field, size_t x, size_t z, double *eta, double *psi);
/* Audits the reconstructed Hamiltonian; residual is measured from independent work/sinks. */
BoatWakeLedger boat_wake_ledger(BoatWake *field);
/* Requested owned bytes, including owner. Excludes allocator overhead and opaque
 * native Accelerate setup allocations; portable transform tables are included. */
size_t boat_wake_storage_bytes(const BoatWake *field);
/* Exact diagonalization of the SAME polynomial on an entirely wet rectangle.
 * Portable mixed-radix cosine transform; dimensions must factor into 2s and 3s.
 * Land-bearing fields keep the water-only graph evaluator. No periodic edges. */
bool boat_wake_use_cosine(BoatWake *field);
typedef struct {
    double height, gradient_x, gradient_z;
    double vertical_velocity, velocity_x, velocity_z;
} BoatWakeSample;
/* Publish one endpoint: A psi is cached for queries AND the independent energy audit.
 * Queries use centred world metres, with cell centres at (i+.5)*dx - extent/2.
 * Degree-eleven tensor interpolation supplies height and its analytic derivatives;
 * horizontal velocity is the gradient of the same interpolated potential.
 * No extrapolation or independent height/velocity clocks. Stencils touching land
 * are refused; near-shore queries are not yet supported by the graph fallback. */
void boat_wake_prepare(BoatWake *field);
bool boat_wake_query(const BoatWake *field, double x, double z, BoatWakeSample *sample);
BoatWakeConfig boat_wake_config(const BoatWake *field); /* Borrowed immutable masks. */
void boat_wake_copy_heights(const BoatWake *field, float *output);
/* Diagnostic nodal peaks using the same interpolation derivatives; not a clamp. */
void boat_wake_extrema(const BoatWake *field, double *height, double *slope);
/* Discrete |D| symbol on unobstructed water, useful for independent dispersion checks. */
double boat_wake_symbol(unsigned order, double spacing, double kx, double kz);
double boat_wake_operator_symbol(
    unsigned order, unsigned spatial_order, double spacing, double kx, double kz);

#endif
