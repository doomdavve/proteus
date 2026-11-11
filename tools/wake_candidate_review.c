/* Exact-backend, shared-query and moving stern source acceptance checks. */
#include "core/boat_wake_player.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define PI 3.14159265358979323846
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void equivalence(void)
{
    /* Exercise portable radix-3, native complex fallback and native real DFT lengths,
     * including transpose tiles smaller than sixteen cells. */
    const size_t widths[] = { 18, 24, 48, 192, 384 };
    for (unsigned size = 0; size < sizeof(widths) / sizeof(widths[0]); ++size) {
        size_t w = widths[size], h = w * 2 / 3, n = w * h;
        double *v = calloc(n, sizeof(double)), *rates = calloc(n, sizeof(double));
        CHECK(v && rates);
        for (size_t i = 0; i < n; ++i) {
            v[i] = .01 * (sin(.72 * i) + cos(.13 * i));
            rates[i] = i % w < 5 ? .2 : 0;
        }
        BoatWakeConfig c = { w, h, .75, 1.0 / 120, 1000, 256, NULL, rates, 8, false };
        BoatWake *a = boat_wake_create(&c), *b = boat_wake_create(&c);
        CHECK(a && b && boat_wake_use_cosine(b));
        double error = 0, scale = 0;
        for (unsigned tick = 0; tick < 12; ++tick) {
            if (tick == 6)
                for (size_t i = 0; i < n; ++i)
                    v[i] = -v[i];
            CHECK(boat_wake_step_with_impulse(a, v) && boat_wake_step_with_impulse(b, v));
            boat_wake_prepare(a);
            boat_wake_prepare(b);
            for (size_t i = 0; i < n; ++i) {
                double ae, ap, be, bp;
                CHECK(boat_wake_sample(a, i % w, i / w, &ae, &ap));
                CHECK(boat_wake_sample(b, i % w, i / w, &be, &bp));
                error += pow(ae - be, 2) + pow(ap - bp, 2);
                scale += ae * ae + ap * ap;
            }
        }
        double relative = sqrt(error / scale);
        CHECK(relative < 1e-10);
        BoatWakeLedger x = boat_wake_ledger(a), y = boat_wake_ledger(b);
        CHECK(fabs(x.injected - y.injected) < 1e-9 * x.positive_work);
        CHECK(fabs(x.retained - y.retained) < 1e-9 * x.positive_work);
        CHECK(fabs(x.absorbed - y.absorbed) < 1e-9 * x.positive_work);
        CHECK(fabs(x.positive_work - y.positive_work) < 1e-9 * x.positive_work);
        CHECK(fabs(x.negative_work - y.negative_work) < 1e-9 * x.positive_work);
        BoatWake *copy = boat_wake_clone(b);
        CHECK(copy);
        CHECK(boat_wake_step(copy) && boat_wake_step(b));
        CHECK(boat_wake_ledger(copy).retained == boat_wake_ledger(b).retained);
        boat_wake_reset(copy);
        CHECK(boat_wake_ledger(copy).retained == 0 && boat_wake_ledger(b).retained > 0);
        printf("equivalence %zux%zu: %.9g\n", w, h, relative);
        boat_wake_destroy(copy);
        boat_wake_destroy(a);
        boat_wake_destroy(b);
        free(v);
        free(rates);
    }
    unsigned char land[24 * 16] = { 0 };
    land[12] = 1;
    BoatWakeConfig c = { 24, 16, .75, 1.0 / 120, 1000, 256, land, NULL, 8, false };
    BoatWake *f = boat_wake_create(&c);
    CHECK(f && !boat_wake_use_cosine(f));
    boat_wake_prepare(f);
    BoatWakeSample blocked;
    CHECK(!boat_wake_query(f, 0, -5, &blocked));
    boat_wake_destroy(f);
    BoatWakePlayer *world = boat_wake_player_create(1.0 / 120);
    CHECK(world);
    BoatWakeStern invalid = { .wet = 1, .vx = NAN };
    CHECK(!boat_wake_player_step(world, invalid, invalid));
    invalid = (BoatWakeStern) { .wet = 1, .drag_power = 1e10 };
    CHECK(!boat_wake_player_step(world, invalid, invalid));
    CHECK(boat_wake_player_report(world).energy.tick == 0);
    boat_wake_player_destroy(world);
}

static void interference(void)
{
    enum { W = 48, H = 32, N = W * H };
    BoatWakeConfig config = { W, H, .75, 1.0 / 120, 1000, 256, NULL, NULL, 8, false };
    BoatWake *a = boat_wake_create(&config), *b = boat_wake_create(&config),
             *sum = boat_wake_create(&config);
    CHECK(a && b && sum && boat_wake_use_cosine(a) && boat_wake_use_cosine(b)
        && boat_wake_use_cosine(sum));
    double p[N], q[N], t[N];
    for (unsigned i = 0; i < N; ++i) {
        p[i] = cos((i % W + .5) * 6 * PI / W);
        q[i] = .8 * cos((i / W + .5) * 4 * PI / H);
        t[i] = p[i] + q[i];
    }
    CHECK(boat_wake_kick(a, p) && boat_wake_kick(b, q) && boat_wake_kick(sum, t));
    for (unsigned tick = 0; tick < 720; ++tick)
        CHECK(boat_wake_step(a) && boat_wake_step(b) && boat_wake_step(sum));
    double error = 0, norm = 0, mean = 0;
    for (unsigned i = 0; i < N; ++i) {
        double ae, ap, be, bp, se, sp;
        CHECK(boat_wake_sample(a, i % W, i / W, &ae, &ap)
            && boat_wake_sample(b, i % W, i / W, &be, &bp)
            && boat_wake_sample(sum, i % W, i / W, &se, &sp));
        error += pow(se - ae - be, 2) + pow(sp - ap - bp, 2);
        norm += se * se + sp * sp;
        mean += se;
        t[i] = -sp;
    }
    CHECK(sqrt(error / norm) < 1e-10 && fabs(mean) < 1e-9);
    BoatWakeLedger before = boat_wake_ledger(sum);
    CHECK(fabs(before.numerical_residual) < .01 * before.positive_work);
    CHECK(boat_wake_kick(sum, t));
    BoatWakeLedger after = boat_wake_ledger(sum);
    CHECK(after.negative_work < 0
        && fabs(after.retained - before.retained - after.negative_work) < 1e-8 * before.retained);
    printf("interference superposition %.9g; mean %.9g; signed cancellation %.9g J\n",
        sqrt(error / norm), mean, after.negative_work);
    boat_wake_destroy(a);
    boat_wake_destroy(b);
    boat_wake_destroy(sum);
}

static void queries(void)
{
    enum { W = 96, H = 64, N = W * H };
    double v[N];
    for (unsigned wavelength = 3; wavelength <= 12; wavelength *= 2) {
        double k = 2 * PI / wavelength, dx = .75, dt = 1.0 / 120;
        for (size_t z = 0; z < H; ++z)
            for (size_t x = 0; x < W; ++x)
                v[z * W + x] = cos(k * (x + .5) * dx);
        BoatWakeConfig c = { W, H, dx, dt, 1000, 256, NULL, NULL, 8, false };
        BoatWake *f = boat_wake_create(&c);
        CHECK(f && boat_wake_use_cosine(f));
        CHECK(boat_wake_kick(f, v) && boat_wake_step(f));
        boat_wake_prepare(f);
        double symbol = boat_wake_operator_symbol(256, 8, dx, k, 0);
        double amplitude = dt * symbol, potential = 1 - .5 * dt * dt * 9.81 * symbol;
        double eh = 0, eg = 0, ev = 0, hu = 0, gu = 0, vu = 0;
        for (unsigned i = 0; i < 400; ++i) {
            double x = -12 + 24 * (i + .37) / 400, z = .123;
            BoatWakeSample q, a, b;
            CHECK(boat_wake_query(f, x, z, &q));
            CHECK(boat_wake_query(f, x - 1e-5, z, &a) && boat_wake_query(f, x + 1e-5, z, &b));
            CHECK(fabs((b.height - a.height) / 2e-5 - q.gradient_x) < 1e-8);
            double h = amplitude * cos(k * (x + W * dx / 2));
            double g = -amplitude * k * sin(k * (x + W * dx / 2));
            double velocity = -potential * k * sin(k * (x + W * dx / 2));
            CHECK(fabs(q.vertical_velocity - q.height / amplitude * symbol * potential) < 1e-9);
            eh += pow(q.height - h, 2);
            eg += pow(q.gradient_x - g, 2);
            ev += pow(q.velocity_x - velocity, 2);
            hu += h * h;
            gu += g * g;
            vu += velocity * velocity;
        }
        printf("query lambda %u: height %.9g gradient %.9g velocity %.9g\n", wavelength,
            sqrt(eh / hu), sqrt(eg / gu), sqrt(ev / vu));
        CHECK(sqrt(eh / hu) < .01 && sqrt(eg / gu) < .01 && sqrt(ev / vu) < .01);
        boat_wake_destroy(f);
    }
}

static BoatWakeStern path(unsigned kind, double t)
{
    BoatWakeStern s = { .wet = 1 };
    double speed = kind == 11 ? 22
        : kind == 10          ? 20
        : kind == 9           ? 24
        : kind == 8           ? 18
        : kind == 7           ? 12
                              : 6;
    if (kind == 1) {
        speed = 2 + t;
        s.x = -20 + 2 * t + .5 * t * t;
        s.vx = speed;
    } else if (kind == 2 || kind == 6) {
        double angle = .7 * t;
        s.x = 8 * cos(angle);
        s.z = 8 * sin(angle);
        s.vx = -5.6 * sin(angle);
        s.vz = 5.6 * cos(angle);
        speed = 5.6;
    } else if (kind == 3) {
        s.x = 20 - 3 * t;
        s.vx = -3;
        speed = 3;
    } else if (kind == 4) {
        speed = t < 3 ? 3 * (1 + cos(PI * t / 3)) : 0;
        s.x = -20 + (t < 3 ? 3 * t + 9 / PI * sin(PI * t / 3) : 9);
        s.vx = speed;
    } else {
        s.x = (kind >= 8 ? -66 : -20) + speed * t;
        s.vx = speed;
    }
    if (kind == 5)
        s.wet = 0; // Entirely dry moving hull.
    if (kind == 6)
        s.wet = .5 * (1 + cos(PI * t / 3)); // Smooth landing/load variation.
    s.resistance = (.22 + .025 * speed) * speed;
    s.drag_power = s.resistance * speed;
    return s;
}

static void moving_sources(void)
{
    for (unsigned kind = 0; kind < 12; ++kind) {
        BoatWakePlayer *a = boat_wake_player_create(1.0 / 120),
                       *b = boat_wake_player_create(1.0 / 240);
        CHECK(a && b);
        double eh = 0, ev = 0, es = 0, hu = 0, vu = 0, su = 0;
        for (unsigned tick = 1; tick <= 720; ++tick) {
            double t = (tick - 1) / 120.0;
            CHECK(boat_wake_player_step(a, path(kind, t), path(kind, t + 1.0 / 120)));
            for (unsigned sub = 0; sub < 2; ++sub) {
                double ts = t + sub / 240.0;
                CHECK(boat_wake_player_step(b, path(kind, ts), path(kind, ts + 1.0 / 240)));
            }
            if (tick % 60 == 0) {
                for (int z = -12; z <= 12; z += 3)
                    for (int x = -24; x <= 24; x += 3) {
                        BoatWakeSample q, r;
                        CHECK(boat_wake_query(boat_wake_player_field(a), x + .173, z + .391, &q));
                        CHECK(boat_wake_query(boat_wake_player_field(b), x + .173, z + .391, &r));
                        eh += pow(q.height - r.height, 2);
                        hu += r.height * r.height;
                        ev += pow(q.vertical_velocity - r.vertical_velocity, 2)
                            + pow(q.velocity_x - r.velocity_x, 2)
                            + pow(q.velocity_z - r.velocity_z, 2);
                        vu += r.vertical_velocity * r.vertical_velocity
                            + r.velocity_x * r.velocity_x + r.velocity_z * r.velocity_z;
                        es += pow(q.gradient_x - r.gradient_x, 2)
                            + pow(q.gradient_z - r.gradient_z, 2);
                        su += r.gradient_x * r.gradient_x + r.gradient_z * r.gradient_z;
                    }
            }
        }
        BoatWakePlayerReport ar = boat_wake_player_report(a), br = boat_wake_player_report(b);
        double energy_error
            = fabs(ar.energy.injected - br.energy.injected) / fmax(1e-20, br.energy.positive_work);
        double height_error = sqrt(eh / fmax(1e-30, hu)),
               velocity_error = sqrt(ev / fmax(1e-30, vu));
        double slope_error = sqrt(es / fmax(1e-30, su));
        printf("source %u: height %.9g velocity %.9g slope %.9g work %.9g; signed %.6g positive "
               "%.6g negative %.6g drag %.6g\n",
            kind, height_error, velocity_error, slope_error, energy_error, ar.energy.injected,
            ar.energy.positive_work, ar.energy.negative_work, ar.resistance_work);
        fflush(stdout);
        CHECK(
            height_error < .01 && velocity_error < .01 && slope_error < .01 && energy_error < .01);
        /* Calibration may allocate more of the existing drag to visible waves,
         * but these prescribed paths must still fit the available work. */
        CHECK(ar.energy.positive_work < ar.resistance_work || kind == 5);
        CHECK(fabs(ar.energy.numerical_residual) < .01 * fmax(1, ar.energy.positive_work));
        if (kind == 5)
            CHECK(ar.energy.injected == 0 && ar.energy.retained == 0);
        double retained = ar.energy.retained;
        boat_wake_player_reset(a);
        ar = boat_wake_player_report(a);
        CHECK(ar.energy.retained == 0 && ar.energy.retired == retained);
        boat_wake_player_destroy(a);
        boat_wake_player_destroy(b);
    }
}

static void basin_domain(void)
{
    BoatWake *f[2];
    double initial[2];
    for (unsigned scale = 1; scale <= 2; ++scale) {
        size_t w = 384 * scale, h = 256 * scale, n = w * h;
        double *rate = calloc(n, sizeof(double)), *v = calloc(n, sizeof(double));
        CHECK(rate && v);
        for (size_t z = 0; z < h; ++z)
            for (size_t x = 0; x < w; ++x) {
                double px = (x + .5 - w * .5) * .75, pz = (z + .5 - h * .5) * .75;
                double edge = fmin(120 - fabs(px), 80 - fabs(pz));
                double t = fmax(0, fmin(1, (10.5 - edge) / 10.5));
                rate[z * w + x] = 2 * t * t;
                /* Central packet plus a source beside the absorbing bank. */
                double a = px + 18, b = px + 108, zz = pz - 55;
                v[z * w + x] = a * exp(-(a * a + pz * pz) / 18) + b * exp(-(b * b + zz * zz) / 18);
            }
        BoatWakeConfig c = { w, h, .75, 1.0 / 120, 1000, 256, NULL, rate, 8, false };
        f[scale - 1] = boat_wake_create(&c);
        CHECK(f[scale - 1] && boat_wake_use_cosine(f[scale - 1]));
        CHECK(boat_wake_kick(f[scale - 1], v));
        initial[scale - 1] = boat_wake_ledger(f[scale - 1]).retained;
        free(rate);
        free(v);
    }
    CHECK(fabs(initial[0] - initial[1]) / initial[1] < .001);
    for (unsigned tick = 1; tick <= 19200; ++tick) {
        CHECK(boat_wake_step(f[0]) && boat_wake_step(f[1]));
        if (tick == 1440 || tick == 4800 || tick == 9600 || tick == 19200) {
            boat_wake_prepare(f[0]);
            boat_wake_prepare(f[1]);
            for (unsigned region = 0; region < 2; ++region) {
                double err = 0, max_height = 0;
                for (int z = -12; z < 12; ++z)
                    for (int x = -12; x < 12; ++x) {
                        BoatWakeSample a, b;
                        double px = x + (region ? -104 : 0), pz = z + (region ? 55 : 0);
                        CHECK(
                            boat_wake_query(f[0], px, pz, &a) && boat_wake_query(f[1], px, pz, &b));
                        err += pow(a.height - b.height, 2);
                        max_height = fmax(max_height, fabs(a.height));
                    }
                /* Normalize by the initial total Hamiltonian-equivalent height norm. */
                double relative = sqrt(err / (initial[1] / (.5 * 1000 * 9.81)));
                printf("basin return %gs region %u: %.9g peak %.9g\n", tick / 120.0, region,
                    relative, max_height);
                fflush(stdout);
                CHECK(relative < .01);
            }
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        BoatWakeLedger l = boat_wake_ledger(f[i]);
        printf("basin %u retained %.9g absorbed %.9g residual %.9g\n", i, l.retained / initial[i],
            l.absorbed / initial[i], l.numerical_residual / initial[i]);
        CHECK(l.retained / initial[i] < .01 && fabs(l.numerical_residual) / initial[i] < .01);
        boat_wake_destroy(f[i]);
    }
}

static int compare_time(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* A size sweep, not a gameplay or smaller-domain acceptance. Every sample includes
 * two 120Hz ticks, a compact moving source, six shared queries/tick, the independent
 * ledger and one float height copy. GPU upload/rendering and the hull are excluded. */
static void costs(void)
{
    const size_t widths[] = { 96, 192, 384, 768 };
    puts("synthetic CPU size sweep: 120Hz, .75m, spatial8/order256; two ticks/frame; "
         "source + field + 12 queries + audit + height copy; excludes hull/GPU");
    for (unsigned size = 0; size < sizeof(widths) / sizeof(widths[0]); ++size) {
        size_t w = widths[size], h = w * 2 / 3, n = w * h;
        double *impulse = calloc(n, sizeof(double)), *rates = calloc(n, sizeof(double));
        float *heights = calloc(n, sizeof(float));
        CHECK(impulse && rates && heights);
        for (size_t z = 0; z < h; ++z)
            for (size_t x = 0; x < w; ++x) {
                double edge = fmin(fmin(x, w - 1 - x), fmin(z, h - 1 - z)) * .75;
                double t = fmax(0, (10.5 - edge) / 10.5);
                rates[z * w + x] = 2 * t * t;
            }
        BoatWakeConfig c = { w, h, .75, 1.0 / 120, 1000, 256, NULL, rates, 8, false };
        BoatWake *f = boat_wake_create(&c);
        CHECK(f && boat_wake_use_cosine(f));
        double times[120], checksum = 0, total = 0;
        for (unsigned frame = 0; frame < 132; ++frame) {
            clock_t start = clock();
            for (unsigned substep = 0; substep < 2; ++substep) {
                double t = (2 * frame + substep + .5) * c.dt;
                double px = 5 * sin(t), pz = 3 * cos(t);
                int cx = (int)floor(px / .75 + w * .5), cz = (int)floor(pz / .75 + h * .5);
                memset(impulse, 0, n * sizeof(double));
                double sum = 0, weight = 0;
                for (int z = cz - 10; z <= cz + 10; ++z)
                    for (int x = cx - 10; x <= cx + 10; ++x) {
                        double dx = (x + .5 - w * .5) * .75 - px;
                        double dz = (z + .5 - h * .5) * .75 - pz;
                        double envelope = exp(-(dx * dx + dz * dz) / 4.5);
                        double v = dx / 1.5 * envelope;
                        impulse[(size_t)z * w + (size_t)x] = v;
                        sum += v;
                        weight += envelope;
                    }
                for (int z = cz - 10; z <= cz + 10; ++z)
                    for (int x = cx - 10; x <= cx + 10; ++x) {
                        double dx = (x + .5 - w * .5) * .75 - px;
                        double dz = (z + .5 - h * .5) * .75 - pz;
                        double envelope = exp(-(dx * dx + dz * dz) / 4.5);
                        size_t i = (size_t)z * w + (size_t)x;
                        impulse[i] = c.dt * (impulse[i] - sum / weight * envelope);
                    }
                CHECK(boat_wake_step_with_impulse(f, impulse));
                boat_wake_prepare(f);
                for (unsigned q = 0; q < 6; ++q) {
                    BoatWakeSample sample;
                    CHECK(boat_wake_query(f, px + q * .17, pz + .39, &sample));
                    checksum += sample.height + sample.vertical_velocity + sample.gradient_x
                        + sample.gradient_z + sample.velocity_x + sample.velocity_z;
                }
                CHECK(isfinite(boat_wake_ledger(f).retained));
            }
            boat_wake_copy_heights(f, heights);
            checksum += heights[n / 2 + w / 2];
            if (frame >= 12) {
                double ms = 1000.0 * (clock() - start) / CLOCKS_PER_SEC;
                times[frame - 12] = ms;
                total += ms;
            }
        }
        qsort(times, 120, sizeof(*times), compare_time);
        printf("%zux%zu cells (%.0fx%.0fm): mean %.6f p50 %.6f p95 %.6f max %.6f ms/frame; "
               "field %zu bytes; source+copy %zu bytes; checksum %.9g\n",
            w, h, w * .75, h * .75, total / 120, .5 * (times[59] + times[60]), times[113],
            times[119], boat_wake_storage_bytes(f), n * (sizeof(double) + sizeof(float)), checksum);
        boat_wake_destroy(f);
        free(heights);
        free(rates);
        free(impulse);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--source") == 0)
        moving_sources();
    else if (argc > 1 && strcmp(argv[1], "--basin-domain") == 0)
        basin_domain();
    else if (argc > 1 && strcmp(argv[1], "--costs") == 0)
        costs();
    else {
        equivalence();
        queries();
        interference();
    }
    return 0;
}
