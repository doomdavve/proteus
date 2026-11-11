/* Isolated runtime checkpoint: temporal convergence and bounded CPU workload. */
#include "core/boat_wake.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979323846
#define G 9.81
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
enum { W = 24, H = 16, N = W * H, MODES = 5, TICKS = 16 * 120 };
static const unsigned mx[MODES] = { 3, 6, 9, 12, 1 }, mz[MODES] = { 0, 2, 1, 4, 1 };
static double basis[MODES][N], symbol[MODES], reference[TICKS + 1][MODES][2];

static double forcing(unsigned m, double t)
{
    /* Smooth accelerate/turn/reverse/stop surrogate projected onto Neumann modes.
     * Prescribed zero-volume forcing, not a calibrated wetted-hull source. */
    double x = 9 + 3 * sin(.7 * t), z = 6 + 2 * sin(.41 * t);
    double gate = t < 12 ? pow(sin(PI * t / 12), 2) : 0;
    return .03 / (1 + m) * gate * cos(PI * mx[m] * x / 18) * cos(PI * mz[m] * z / 12);
}

static void derivative(unsigned m, double t, const double *s, double *d)
{
    d[0] = symbol[m] * s[1];
    d[1] = -G * s[0] + forcing(m, t);
}

static void prepare(void)
{
    for (unsigned m = 0; m < MODES; ++m) {
        symbol[m] = boat_wake_operator_symbol(256, 8, .75, PI * mx[m] / 18, PI * mz[m] / 12);
        for (unsigned z = 0; z < H; ++z)
            for (unsigned x = 0; x < W; ++x)
                basis[m][z * W + x]
                    = cos(PI * mx[m] * (x + .5) / W) * cos(PI * mz[m] * (z + .5) / H);
        double s[2] = { 0, 0 };
        /* Independent RK4 modal ODE reference; sixteen substeps per 120 Hz sample. */
        const double dt = 1.0 / 1920;
        for (unsigned tick = 1; tick <= TICKS; ++tick) {
            for (unsigned sub = 0; sub < 16; ++sub) {
                double t = ((tick - 1) * 16 + sub) * dt, k[4][2], tmp[2];
                derivative(m, t, s, k[0]);
                for (unsigned j = 0; j < 2; ++j)
                    tmp[j] = s[j] + .5 * dt * k[0][j];
                derivative(m, t + .5 * dt, tmp, k[1]);
                for (unsigned j = 0; j < 2; ++j)
                    tmp[j] = s[j] + .5 * dt * k[1][j];
                derivative(m, t + .5 * dt, tmp, k[2]);
                for (unsigned j = 0; j < 2; ++j)
                    tmp[j] = s[j] + dt * k[2][j];
                derivative(m, t + dt, tmp, k[3]);
                for (unsigned j = 0; j < 2; ++j)
                    s[j] += dt / 6 * (k[0][j] + 2 * k[1][j] + 2 * k[2][j] + k[3][j]);
            }
            memcpy(reference[tick][m], s, sizeof(s));
        }
    }
}

static void source(double *impulse, double t, double duration)
{
    memset(impulse, 0, N * sizeof(double));
    for (unsigned m = 0; m < MODES; ++m) {
        double amplitude = forcing(m, t) * duration;
        for (unsigned i = 0; i < N; ++i)
            impulse[i] += amplitude * basis[m][i];
    }
}

static void snapshot(BoatWake *f, double state[N][2])
{
    for (unsigned i = 0; i < N; ++i)
        REQUIRE(boat_wake_sample(f, i % W, i / W, &state[i][0], &state[i][1]));
}

static void cadence(void)
{
    prepare();
    double previous_error = 1;
    puts("\"cadence\": [");
    for (unsigned hz = 30; hz <= 120; hz *= 2) {
        BoatWakeConfig c = { W, H, .75, 1.0 / hz, 1000, 256, NULL, NULL, 8, false };
        BoatWake *f = boat_wake_create(&c);
        REQUIRE(f);
        double old[N][2] = { { 0 } }, next[N][2], impulse[N];
        double error[3] = { 0 }, norm[3] = { 0 };
        unsigned stride = 120 / hz;
        for (unsigned step = 0; step < 16 * hz; ++step) {
            source(impulse, (step + .5) * c.dt, c.dt);
            REQUIRE(boat_wake_step_with_impulse(f, impulse));
            snapshot(f, next);
            for (unsigned sub = 1; sub <= stride; ++sub) {
                unsigned tick = step * stride + sub;
                double alpha = (double)sub / stride;
                double projected[MODES];
                for (unsigned m = 0; m < MODES; ++m) {
                    double coefficient = 0, normalization = 0;
                    for (unsigned j = 0; j < N; ++j) {
                        coefficient += basis[m][j] * ((1 - alpha) * old[j][1] + alpha * next[j][1]);
                        normalization += basis[m][j] * basis[m][j];
                    }
                    projected[m] = coefficient / normalization;
                }
                for (unsigned i = 0; i < N; ++i) {
                    double exact[3] = { 0 }, approximate[3] = { 0 };
                    for (unsigned m = 0; m < MODES; ++m) {
                        exact[0] += basis[m][i] * reference[tick][m][0];
                        exact[1] += basis[m][i] * reference[tick][m][1];
                        exact[2] += basis[m][i] * symbol[m] * reference[tick][m][1];
                    }
                    approximate[0] = (1 - alpha) * old[i][0] + alpha * next[i][0];
                    approximate[1] = (1 - alpha) * old[i][1] + alpha * next[i][1];
                    /* Projection of the same interpolated potential gives A psi.
                     * This is a diagnostic oracle, not a free runtime query API. */
                    for (unsigned m = 0; m < MODES; ++m)
                        approximate[2] += basis[m][i] * symbol[m] * projected[m];
                    for (unsigned j = 0; j < 3; ++j) {
                        error[j] += pow(approximate[j] - exact[j], 2);
                        norm[j] += exact[j] * exact[j];
                    }
                }
            }
            memcpy(old, next, sizeof(old));
        }
        for (unsigned j = 0; j < 3; ++j)
            error[j] = sqrt(error[j] / norm[j]);
        printf("%s{\"hz\": %u, \"eta_l2\": %.9g, \"psi_l2\": %.9g, \"eta_t_l2\": %.9g, "
               "\"within_one_percent\": %s}",
            hz == 30 ? "" : ",\n", hz, error[0], error[1], error[2],
            error[0] < .01 && error[1] < .01 && error[2] < .01 ? "true" : "false");
        REQUIRE(
            error[0] < .03 && error[1] < .03 && error[2] < .03 && error[0] < previous_error * .35);
        REQUIRE(hz == 30 || (error[0] < .01 && error[1] < .01 && error[2] < .01));
        previous_error = error[0];
        boat_wake_destroy(f);
    }
    puts("\n]");
}

static void workload(void)
{
    enum { BW = 192, BH = 128, BN = BW * BH };
    double *impulse = calloc(BN, sizeof(double));
    REQUIRE(impulse);
    puts("\"cpu_workload\": [");
    for (unsigned hz = 30; hz <= 120; hz *= 2) {
        BoatWakeConfig c = { BW, BH, .75, 1.0 / hz, 1000, 256, NULL, NULL, 8, false };
        BoatWake *f = boat_wake_create(&c);
        REQUIRE(f);
        for (unsigned i = 0; i < BN; ++i)
            impulse[i] = .001 * cos(PI * 48 * (i % BW + .5) / BW);
        for (unsigned step = 0; step < 4; ++step)
            REQUIRE(boat_wake_step_with_impulse(f, impulse));
        clock_t start = clock();
        double checksum = 0;
        for (unsigned step = 0; step < 24; ++step) {
            REQUIRE(boat_wake_step_with_impulse(f, impulse));
            for (unsigned support = 0; support < 6; ++support) {
                double eta, psi;
                REQUIRE(boat_wake_sample(f, 80 + support, 60, &eta, &psi));
                checksum += eta + psi;
            }
            /* A complete Hamiltonian audit every update, also a conservative proxy
             * for the additional A psi needed by exact endpoint eta_t queries. */
            checksum += boat_wake_ledger(f).retained;
        }
        double ms = 1000.0 * (clock() - start) / CLOCKS_PER_SEC / 24;
        REQUIRE(isfinite(checksum));
        printf("%s{\"hz\": %u, \"symmetric_source_step_audit_six_samples_ms\": %.6f, "
               "\"ms_per_60hz_frame\": "
               "%.6f, \"owned_bytes\": %zu}",
            hz == 30 ? "" : ",\n", hz, ms, ms * hz / 60, boat_wake_storage_bytes(f));
        boat_wake_destroy(f);
    }
    free(impulse);
    puts("\n]");
}

int main(int argc, char **argv)
{
    puts("{");
    if (argc == 2 && strcmp(argv[1], "--costs") == 0)
        workload();
    else if (argc == 1 || (argc == 2 && strcmp(argv[1], "--check") == 0))
        cadence();
    else
        return 2;
    puts("}");
    return 0;
}
