#include "boat_wake.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define W 48
#define H 32
#define N (W * H)
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static double impulse[N];
static unsigned char land[N];
static double rates[N];

static BoatWake *make(unsigned mode, double dt, unsigned order)
{
    for (size_t z = 0; z < H; ++z) {
        for (size_t x = 0; x < W; ++x) {
            size_t i = z * W + x;
            land[i] = (mode == 2 || mode == 3) && x >= 23 && x <= 26
                && (mode == 3 || (z >= 7 && z <= 24));
            double edge = fmin(fmin(x, W - 1 - x), fmin(z, H - 1 - z));
            rates[i] = mode && edge < 7 ? 2 * pow((7 - edge) / 7, 2) : 0;
            if (mode == 2 && !land[i] && x >= 21 && x <= 28 && z >= 5 && z <= 26)
                rates[i] = fmax(rates[i], 1.0); /* Explicit absorbing shore. */
            double dx = (double)x - 12, dz = (double)z - 16;
            impulse[i]
                = land[i] || dx * dx + dz * dz >= 36 ? 0 : dx * exp(-(dx * dx + dz * dz) / 8);
        }
    }
    BoatWakeConfig c = { W, H, 1.5, dt, 1000, order, land, rates, 2, false };
    BoatWake *f = boat_wake_create(&c);
    REQUIRE(f);
    REQUIRE(boat_wake_kick(f, impulse));
    return f;
}

static void ledger(BoatWake *f)
{
    BoatWakeLedger l = boat_wake_ledger(f);
    double error = l.retained + l.absorbed + l.retired - l.injected - l.numerical_residual;
    REQUIRE(fabs(error) < 1e-9 * fmax(1, fabs(l.injected)));
    if (!(l.absorbed >= 0 && l.retired >= 0 && isfinite(l.retained)))
        fprintf(stderr, "ledger retained=%g absorbed=%g retired=%g residual=%g\n", l.retained,
            l.absorbed, l.retired, l.numerical_residual);
    REQUIRE(l.absorbed >= 0 && l.retired >= 0 && isfinite(l.retained));
}

static double region(BoatWake *f)
{
    double sum = 0;
    for (size_t z = 12; z <= 20; ++z) {
        for (size_t x = 30; x <= 36; ++x) {
            double eta, psi;
            REQUIRE(boat_wake_sample(f, x, z, &eta, &psi));
            sum += eta * eta;
        }
    }
    return sum;
}

static void picture(FILE *out, BoatWake **fields)
{
    if (!out)
        return;
    for (size_t z = 0; z < H; ++z) {
        for (unsigned mode = 0; mode < 4; ++mode) {
            for (size_t x = 0; x < W; ++x) {
                double eta, psi;
                boat_wake_sample(fields[mode], x, z, &eta, &psi);
                double value = fmax(-1, fmin(1, eta / 0.03));
                unsigned char rgb[3] = { (unsigned char)(128 + 127 * value),
                    (unsigned char)(128 - 100 * fabs(value)), (unsigned char)(128 - 127 * value) };
                if (mode >= 2 && x >= 23 && x <= 26 && (mode == 3 || (z >= 7 && z <= 24)))
                    rgb[0] = rgb[1] = rgb[2] = 20;
                fwrite(rgb, 1, 3, out);
            }
        }
    }
}

int main(int argc, char **argv)
{
    REQUIRE(!boat_wake_create(NULL));
    boat_wake_destroy(NULL);
    /* Ownership, copied config, negative work, atomic invalid kick, retirement. */
    BoatWake *f = make(0, 0.05, 64);
    BoatWake *copy = boat_wake_clone(f);
    REQUIRE(copy);
    double initial = boat_wake_ledger(f).retained;
    for (size_t i = 0; i < N; ++i)
        impulse[i] *= -0.5;
    REQUIRE(boat_wake_kick(f, impulse));
    REQUIRE(boat_wake_ledger(f).negative_work < 0);
    REQUIRE(boat_wake_ledger(copy).retained == initial);
    impulse[N - 1] = DBL_MAX;
    REQUIRE(!boat_wake_kick(f, impulse));
    impulse[N - 1] = NAN;
    double saved = boat_wake_ledger(f).retained;
    REQUIRE(!boat_wake_kick(f, impulse));
    REQUIRE(boat_wake_ledger(f).retained == saved);
    boat_wake_reset(f);
    REQUIRE(boat_wake_ledger(f).retained == 0);
    ledger(f);
    boat_wake_destroy(f);
    REQUIRE(boat_wake_step(copy));
    ledger(copy);
    boat_wake_destroy(copy);

    /* Time convergence measured independently of residual ledger closure. */
    double errors[2];
    for (unsigned r = 0; r < 2; ++r) {
        f = make(0, 0.05 / (r + 1), 64);
        initial = boat_wake_ledger(f).retained;
        errors[r] = 0;
        for (unsigned t = 0; t < 400 * (r + 1); ++t) {
            REQUIRE(boat_wake_step(f));
            double drift = fabs(boat_wake_ledger(f).retained / initial - 1);
            if (drift > errors[r])
                errors[r] = drift;
        }
        double mean = 0;
        for (size_t z = 0; z < H; ++z)
            for (size_t x = 0; x < W; ++x) {
                double eta, psi;
                REQUIRE(boat_wake_sample(f, x, z, &eta, &psi));
                mean += eta;
            }
        REQUIRE(fabs(mean / N) < 1e-12);
        ledger(f);
        boat_wake_destroy(f);
    }
    printf("source-off max relative drift dt=.05/.025: %.9g %.9g\n", errors[0], errors[1]);
    REQUIRE(errors[0] < 0.01 && errors[1] < errors[0] * 0.3);
    /* Symbol approximation is compared to the graph sqrt, separately from spatial error. */
    double k = 2 * 3.14159265358979323846 / 6;
    double graph = 2 * sin(k * 1.5 / 2) / 1.5;
    double symbol = boat_wake_symbol(128, 1.5, k, 0);
    printf("6m |D|: polynomial %.9g graph %.9g continuum %.9g\n", symbol, graph, k);
    REQUIRE(fabs(symbol / graph - 1) < 0.01);

    /* Independent Neumann cosine mode: tests polynomial action, phase and sign. */
    f = make(0, 0.025, 128);
    boat_wake_reset(f);
    double mode_k = 8 * 3.14159265358979323846 / (W * 1.5);
    for (size_t z = 0; z < H; ++z)
        for (size_t x = 0; x < W; ++x)
            impulse[z * W + x] = cos(mode_k * (x + 0.5) * 1.5);
    REQUIRE(boat_wake_kick(f, impulse));
    copy = boat_wake_clone(f);
    REQUIRE(copy);
    for (unsigned t = 0; t < 80; ++t) {
        REQUIRE(boat_wake_step(f));
        REQUIRE(boat_wake_step(copy));
    }
    double symbol_mode = boat_wake_symbol(128, 1.5, mode_k, 0);
    double omega = sqrt(9.81 * symbol_mode);
    double mode_error = 0;
    for (size_t z = 0; z < H; ++z)
        for (size_t x = 0; x < W; ++x) {
            double eta, psi, ce, cp;
            REQUIRE(boat_wake_sample(f, x, z, &eta, &psi));
            REQUIRE(boat_wake_sample(copy, x, z, &ce, &cp));
            REQUIRE(eta == ce && psi == cp);
            double reference = symbol_mode / omega * sin(omega * 2) * impulse[z * W + x];
            mode_error = fmax(mode_error, fabs(eta - reference));
        }
    printf("manufactured cosine eta max absolute error: %.9g\n", mode_error);
    REQUIRE(mode_error < 0.001);
    boat_wake_destroy(copy);
    boat_wake_destroy(f);
    /* Costs for candidate physical domains; no graphics context or FFT dependency. */
    for (unsigned size = 48; size <= 96; size *= 2) {
        BoatWakeConfig config = { size, size, 0.75, 0.025, 1000, 128, NULL, NULL, 2, false };
        BoatWake *bench = boat_wake_create(&config);
        REQUIRE(bench);
        clock_t begin = clock();
        for (unsigned t = 0; t < 20; ++t)
            REQUIRE(boat_wake_step(bench));
        printf("cost %ux%u order128: %.6f ms/step, owned payload %zu bytes\n", size, size,
            1000.0 * (clock() - begin) / CLOCKS_PER_SEC / 20, boat_wake_storage_bytes(bench));
        boat_wake_destroy(bench);
    }
    for (unsigned r = 0; r < 3; ++r) {
        double spacing = 1.5 / (1u << r);
        double discrete = boat_wake_symbol(128, spacing, k, 0);
        printf(
            "6m spacing %.3f phase speed relative error %.9g\n", spacing, sqrt(discrete / k) - 1);
    }
    BoatWake *fields[4];
    double peak[4] = { 0 }, late[4] = { 0 }, returning[4] = { 0 }, start[4];
    for (unsigned m = 0; m < 4; ++m) {
        fields[m] = make(m, 0.05, 64);
        start[m] = boat_wake_ledger(fields[m]).retained;
    }
    FILE *out = argc > 1 ? fopen(argv[1], "wb") : NULL;
    if (argc > 1)
        REQUIRE(out);
    if (out)
        fprintf(out, "P6\n%d %d\n255\n", W * 4, H * 5);
    clock_t begin = clock();
    for (unsigned t = 0; t < 1600; ++t) {
        for (unsigned m = 0; m < 4; ++m) {
            REQUIRE(boat_wake_step(fields[m]));
            double value = region(fields[m]);
            if (t < 240 && value > peak[m])
                peak[m] = value;
            if (t >= 240 && value > late[m])
                late[m] = value;
            if (t >= 800 && value > returning[m])
                returning[m] = value;
            if (m == 3)
                REQUIRE(value == 0); /* Including long propagation and return. */
        }
        if (t == 79 || t == 159 || t == 239 || t == 799 || t == 1599)
            picture(out, fields);
    }
    if (out)
        fclose(out);
    printf(
        "four 72x48m fields, 80s: %.3f CPU seconds\n", (double)(clock() - begin) / CLOCKS_PER_SEC);
    for (unsigned m = 0; m < 4; ++m) {
        BoatWakeLedger l = boat_wake_ledger(fields[m]);
        ledger(fields[m]);
        printf("mode %u early/late shadow %.9g %.9g retained %.6f absorbed %.6f residual %.6f\n", m,
            peak[m], late[m], l.retained / start[m], l.absorbed / start[m],
            l.numerical_residual / start[m]);
    }
    REQUIRE(peak[1] > 1e-8);
    REQUIRE(peak[2] < peak[1] * 0.1);
    REQUIRE(late[2] < late[1] * 0.2);
    REQUIRE(boat_wake_ledger(fields[1]).retained < start[1] * 0.05);
    printf("40-80s return region open/closed: %.9g %.9g\n", returning[1], returning[0]);
    REQUIRE(returning[1] < returning[0] * 0.2);
    /* Leaving an observation region never mutates state; snapshot survives reset/destroy. */
    copy = boat_wake_clone(fields[2]);
    REQUIRE(copy);
    initial = boat_wake_ledger(copy).retained;
    boat_wake_reset(fields[2]);
    ledger(fields[2]);
    for (unsigned m = 0; m < 4; ++m)
        boat_wake_destroy(fields[m]);
    REQUIRE(boat_wake_ledger(copy).retained == initial);
    REQUIRE(boat_wake_step(copy));
    ledger(copy);
    boat_wake_destroy(copy);
    puts("owned wake checks passed");
    return 0;
}
