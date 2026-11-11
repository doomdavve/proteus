/* Isolated world-space source-packet / spectral-quadrature experiment.
 * No runtime imports this code. A fixed quadrature is NOT an unbounded-domain solver:
 * its implicit spatial replicas and growing reconstruction cost are measured here. */
#include "core/boat_wake.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979323846
#define DT (1.0 / 120)
#define G 9.81
#define SIGMA 1.5
#define CAPACITY 8
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

typedef struct {
    double x, z, nx, nz, strength;
    unsigned tick;
} Packet;
typedef struct {
    double re, im;
} Complex;
typedef struct {
    double kx, kz, symbol, theta, weight;
    Complex eta, psi;
} Node;
typedef struct {
    Packet packets[CAPACITY];
    size_t count, rejected, nodes;
    Node *node;
    double origin_x, origin_z, injected, retired;
    unsigned tick;
} Packets;
typedef struct {
    double eta, psi, vertical, gx, gz, vx, vz;
} Sample;

static double seconds(void) { return (double)clock() / CLOCKS_PER_SEC; }

static Packets create(unsigned resolution)
{
    REQUIRE(resolution >= 16 && resolution % 2 == 0);
    Packets p = { .nodes = (size_t)resolution * resolution / 2 };
    p.node = calloc(p.nodes, sizeof(*p.node));
    REQUIRE(p.node);
    /* Midpoint integration over half of [-4,4]^2 rad/m; the conjugate half is
     * incorporated in weight. Shared nodes are not a world-sized field. */
    double dk = 8.0 / resolution;
    for (unsigned z = 0; z < resolution; ++z)
        for (unsigned x = 0; x < resolution / 2; ++x) {
            Node *n = &p.node[z * resolution / 2 + x];
            n->kx = (x + .5) * dk;
            n->kz = -4 + (z + .5) * dk;
            n->symbol = boat_wake_operator_symbol(256, 8, .75, n->kx, n->kz);
            n->theta = acos(1 - .5 * DT * DT * G * n->symbol);
            n->weight = 2 * dk * dk / (4 * PI * PI);
        }
    return p;
}

static double energy(const Packets *p)
{
    double h = 0;
    for (size_t j = 0; j < p->nodes; ++j) {
        const Node *n = &p->node[j];
        h += n->weight
            * (G * (n->eta.re * n->eta.re + n->eta.im * n->eta.im)
                + n->symbol * (n->psi.re * n->psi.re + n->psi.im * n->psi.im));
    }
    return 500 * h;
}

static void prepare(Packets *p, unsigned tick)
{
    p->tick = tick;
    for (size_t j = 0; j < p->nodes; ++j) {
        Node *n = &p->node[j];
        n->eta = n->psi = (Complex) { 0 };
        for (size_t i = 0; i < p->count; ++i) {
            Packet a = p->packets[i];
            REQUIRE(tick >= a.tick);
            double phase = -n->kx * (a.x - p->origin_x) - n->kz * (a.z - p->origin_z);
            double envelope = exp(-.5 * SIGMA * SIGMA * (n->kx * n->kx + n->kz * n->kz));
            /* Fourier transform of -strength (r.n/sigma) exp(-r²/2sigma²).
             * This matches the production source's untruncated spatial recipe,
             * but uses prescribed impulses, not calibrated moving hull work. */
            double amplitude
                = a.strength * 2 * PI * pow(SIGMA, 3) * (n->kx * a.nx + n->kz * a.nz) * envelope;
            Complex impulse = { -amplitude * sin(phase), amplitude * cos(phase) };
            double age = (tick - a.tick) * n->theta;
            double aeta = DT * n->symbol * sin(age) / sin(n->theta), apsi = cos(age);
            n->eta.re += aeta * impulse.re;
            n->eta.im += aeta * impulse.im;
            n->psi.re += apsi * impulse.re;
            n->psi.im += apsi * impulse.im;
        }
    }
}

static bool append(Packets *p, Packet a)
{
    /* Reject without overwriting, merging or retiring live waves. Caller must
     * handle this failure. This is an experimental limit, not a gameplay policy. */
    if (p->count == CAPACITY) {
        ++p->rejected;
        return false;
    }
    prepare(p, a.tick);
    double before = energy(p);
    p->packets[p->count++] = a;
    prepare(p, a.tick);
    p->injected += energy(p) - before; /* Signed combined-field work. */
    return true;
}

static Sample query(const Packets *p, double x, double z)
{
    Sample s = { 0 };
    x -= p->origin_x;
    z -= p->origin_z;
    for (size_t j = 0; j < p->nodes; ++j) {
        const Node *n = &p->node[j];
        double phase = n->kx * x + n->kz * z;
        double re = n->weight * cos(phase), im = n->weight * sin(phase);
        Complex h = { re * n->eta.re - im * n->eta.im, re * n->eta.im + im * n->eta.re };
        Complex v = { re * n->psi.re - im * n->psi.im, re * n->psi.im + im * n->psi.re };
        s.eta += h.re;
        s.psi += v.re;
        s.vertical += n->symbol * v.re;
        s.gx -= n->kx * h.im;
        s.gz -= n->kz * h.im;
        s.vx -= n->kx * v.im;
        s.vz -= n->kz * v.im;
    }
    return s;
}

static double height_at(const Packets *p, double x, double z)
{
    double h = 0;
    x -= p->origin_x;
    z -= p->origin_z;
    for (size_t j = 0; j < p->nodes; ++j) {
        const Node *n = &p->node[j];
        double phase = n->kx * x + n->kz * z;
        h += n->weight * (cos(phase) * n->eta.re - sin(phase) * n->eta.im);
    }
    return h;
}

static double norm(Sample a)
{
    return a.eta * a.eta + a.psi * a.psi + a.vertical * a.vertical + a.gx * a.gx + a.gz * a.gz
        + a.vx * a.vx + a.vz * a.vz;
}

static Sample subtract(Sample a, Sample b)
{
    return (Sample) { a.eta - b.eta, a.psi - b.psi, a.vertical - b.vertical, a.gx - b.gx,
        a.gz - b.gz, a.vx - b.vx, a.vz - b.vz };
}

static void retire_all(Packets *p)
{
    p->retired += energy(p);
    p->count = 0;
    prepare(p, p->tick);
}

static void checks(void)
{
    Packets p = create(64);
    Packet a = { 0, 0, 1, 0, 1, 0 };
    REQUIRE(append(&p, a));
    Sample s = query(&p, 1.1, .7);
    double exact = -1.1 / SIGMA * exp(-.5 * (1.1 * 1.1 + .7 * .7) / (SIGMA * SIGMA));
    REQUIRE(fabs(s.psi - exact) < 1e-7);
    prepare(&p, 1200);
    s = query(&p, 1.1, .7);
    double eps = 1e-5;
    Sample left = query(&p, 1.1 - eps, .7), right = query(&p, 1.1 + eps, .7);
    REQUIRE(fabs((right.eta - left.eta) / (2 * eps) - s.gx) < 1e-8);
    REQUIRE(fabs((right.psi - left.psi) / (2 * eps) - s.vx) < 1e-8);
    /* Moving the query region does not mutate the packets or their phase. */
    (void)query(&p, 10000, -10000);
    REQUIRE(norm(subtract(s, query(&p, 1.1, .7))) == 0);
    p.origin_x = p.origin_z = 1000000;
    p.packets[0].x += 1000000;
    p.packets[0].z += 1000000;
    prepare(&p, 1200);
    REQUIRE(norm(subtract(s, query(&p, 1000001.1, 1000000.7))) < 1e-19);
    /* Coincident opposite impulses cancel both fields and book negative work. */
    a.x = a.z = 1000000;
    a.strength = -1;
    REQUIRE(append(&p, a));
    prepare(&p, 1200);
    REQUIRE(energy(&p) < 1e-24 && fabs(p.injected) < 1e-9);
    while (p.count < CAPACITY)
        REQUIRE(append(&p, a));
    double retained = energy(&p), injected = p.injected;
    for (unsigned i = 0; i < 100000; ++i)
        REQUIRE(!append(&p, a));
    REQUIRE(p.count == CAPACITY && energy(&p) == retained && p.injected == injected);
    REQUIRE(p.rejected == 100000);
    retire_all(&p);
    REQUIRE(energy(&p) == 0 && p.retired == retained);
    REQUIRE(fabs(p.retired - p.injected) < 1e-8);
    free(p.node);
}

static void kick_reference(BoatWake *field, double *impulse, Packet a)
{
    BoatWakeConfig c = boat_wake_config(field);
    double sum = 0, weights = 0;
    for (size_t z = 0; z < c.height; ++z)
        for (size_t x = 0; x < c.width; ++x) {
            double dx = (x + .5 - c.width * .5) * c.spacing - a.x;
            double dz = (z + .5 - c.height * .5) * c.spacing - a.z;
            double w = fabs(dx) <= 7.5 && fabs(dz) <= 7.5
                ? exp(-.5 * (dx * dx + dz * dz) / (SIGMA * SIGMA))
                : 0;
            double v = -a.strength * (dx * a.nx + dz * a.nz) / SIGMA * w;
            impulse[z * c.width + x] = v;
            sum += v;
            weights += w;
        }
    for (size_t z = 0; z < c.height; ++z)
        for (size_t x = 0; x < c.width; ++x) {
            double dx = (x + .5 - c.width * .5) * c.spacing - a.x;
            double dz = (z + .5 - c.height * .5) * c.spacing - a.z;
            double w = fabs(dx) <= 7.5 && fabs(dz) <= 7.5
                ? exp(-.5 * (dx * dx + dz * dz) / (SIGMA * SIGMA))
                : 0;
            impulse[z * c.width + x] -= sum / weights * w;
        }
    REQUIRE(boat_wake_kick(field, impulse));
}

static void reference_check(void)
{
    BoatWakeConfig c = { 96, 64, .75, DT, 1000, 256, NULL, NULL, 8, false };
    BoatWake *field = boat_wake_create(&c);
    REQUIRE(field && boat_wake_use_cosine(field));
    double *impulse = calloc(c.width * c.height, sizeof(double));
    REQUIRE(impulse);
    Packets p = create(128);
    Packet a = { 0, 0, .6, .8, 1, 0 };
    REQUIRE(append(&p, a));
    kick_reference(field, impulse, a);
    for (unsigned tick = 0; tick <= 120; ++tick) {
        if (tick % 60 == 0) {
            prepare(&p, tick);
            boat_wake_prepare(field);
            double error = 0, scale = 0;
            for (unsigned i = 0; i < 9; ++i) {
                double x = -4 + (double)i + .13, z = .27;
                BoatWakeSample q;
                REQUIRE(boat_wake_query(field, x, z, &q));
                Sample r = { q.height, 0, q.vertical_velocity, q.gradient_x, q.gradient_z,
                    q.velocity_x, q.velocity_z };
                Sample s = query(&p, x, z);
                s.psi = 0;
                error += norm(subtract(s, r));
                scale += norm(r);
            }
            REQUIRE(sqrt(error / scale) < .001);
            REQUIRE(fabs(energy(&p) / boat_wake_ledger(field).retained - 1) < .0001);
        }
        if (tick < 120)
            REQUIRE(boat_wake_step(field));
    }
    free(p.node);
    free(impulse);
    boat_wake_destroy(field);
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void review(unsigned resolution, bool doubled, const char *csv_path)
{
    Packets p = create(resolution);
    size_t width = doubled ? 768 : 384, height = doubled ? 512 : 256;
    BoatWakeConfig c = { width, height, .75, DT, 1000, 256, NULL, NULL, 8, false };
    BoatWake *reference = boat_wake_create(&c);
    REQUIRE(reference && boat_wake_use_cosine(reference));
    double *impulse = calloc(width * height, sizeof(double));
    REQUIRE(impulse);
    FILE *csv = csv_path ? fopen(csv_path, "w") : NULL;
    REQUIRE(!csv_path || csv);
    if (csv)
        fprintf(csv, "time,x,z,reference,packet\n");
    double history_error[6] = { 0 }, history_scale[6] = { 0 };
    /* Two separated moving source histories; four actual spatial impulses each.
     * Deliberately not the production midpoint/symmetric source-time integration. */
    for (unsigned tick = 0; tick <= 2880; ++tick) {
        if (tick <= 180 && tick % 60 == 0) {
            double t = tick * DT;
            Packet a = { -6 + 4 * t, -4, 1, 0, .25, tick };
            Packet b = { 4, -6 + 4 * t, 0, 1, .25, tick };
            REQUIRE(append(&p, a) && append(&p, b));
            kick_reference(reference, impulse, a);
            kick_reference(reference, impulse, b);
        }
        if (tick % 120 == 0 && tick >= 240) {
            prepare(&p, tick);
            boat_wake_prepare(reference);
            double error[6] = { 0 }, scale[6] = { 0 }, encounter_error = 0, encounter_scale = 0;
            /* Two detail regions separate and overlap repeatedly. A third path
             * stays near the old wake: do not hide its errors while away. */
            for (unsigned region = 0; region < 3; ++region) {
                double t = tick * DT;
                /* Return through the outward-moving wake, not just the nearly
                 * empty source position. Other regions retain that control. */
                double centre = region == 0 ? 70 * sin(PI * (t - 2) / 22) + 1.6 * (t - 2)
                    : region == 1           ? -55 * sin(2 * PI * (t - 2) / 22)
                                            : 0;
                for (int z = -2; z <= 2; ++z)
                    for (int x = -2; x <= 2; ++x) {
                        double px = centre + x * 1.17 + .13, pz = z * 1.13 + .21;
                        BoatWakeSample q;
                        REQUIRE(boat_wake_query(reference, px, pz, &q));
                        Sample s = query(&p, px, pz);
                        if (region == 0) {
                            encounter_error += pow(s.eta - q.height, 2);
                            encounter_scale += q.height * q.height;
                        }
                        double a[] = { s.eta, s.vertical, s.gx, s.vx, s.gz, s.vz };
                        double b[] = { q.height, q.vertical_velocity, q.gradient_x, q.velocity_x,
                            q.gradient_z, q.velocity_z };
                        for (unsigned j = 0; j < 6; ++j) {
                            error[j] += pow(a[j] - b[j], 2);
                            scale[j] += b[j] * b[j];
                        }
                    }
            }
            for (unsigned j = 0; j < 6; ++j) {
                history_error[j] += error[j];
                history_scale[j] += scale[j];
            }
            double h = energy(&p);
            BoatWakeLedger ledger = boat_wake_ledger(reference);
            printf("{\"kind\":\"accuracy\",\"resolution\":%u,\"doubled\":%s,\"time\":%.0f,"
                   "\"height_l2\":%.9g,\"vertical_l2\":%.9g,\"gradient_x_l2\":%.9g,"
                   "\"velocity_x_l2\":%.9g,\"gradient_z_l2\":%.9g,\"velocity_z_l2\":%.9g,"
                   "\"energy_relative\":%.9g,"
                   "\"height_rms_error_m\":%.9g,\"reference_rms_height_m\":%.9g,"
                   "\"encounter_height_l2\":%.9g,\"encounter_reference_rms_m\":%.9g,"
                   "\"packet_energy\":%.9g,\"source_work\":%.9g,\"residual\":%.9g}\n",
                resolution, doubled ? "true" : "false", tick * DT,
                sqrt(error[0] / fmax(scale[0], 1e-30)), sqrt(error[1] / fmax(scale[1], 1e-30)),
                sqrt(error[2] / fmax(scale[2], 1e-30)), sqrt(error[3] / fmax(scale[3], 1e-30)),
                sqrt(error[4] / fmax(scale[4], 1e-30)), sqrt(error[5] / fmax(scale[5], 1e-30)),
                fabs(h - ledger.retained) / ledger.retained, sqrt(error[0] / 75),
                sqrt(scale[0] / 75), sqrt(encounter_error / fmax(encounter_scale, 1e-30)),
                sqrt(encounter_scale / 25), h, p.injected, h - p.injected);
            if (csv && (tick == 480 || tick == 2880))
                for (int z = -24; z <= 24; ++z)
                    for (int x = -90; x <= 90; ++x) {
                        BoatWakeSample q;
                        REQUIRE(boat_wake_query(reference, x, z, &q));
                        fprintf(csv, "%.0f,%d,%d,%.9g,%.9g\n", tick * DT, x, z, q.height,
                            height_at(&p, x, z));
                    }
        }
        if (tick < 2880)
            REQUIRE(boat_wake_step(reference));
    }
    bool accurate = true;
    for (unsigned j = 0; j < 6; ++j)
        accurate = accurate && history_error[j] < .0001 * history_scale[j];
    printf("{\"kind\":\"history\",\"resolution\":%u,\"height_l2\":%.9g,"
           "\"vertical_l2\":%.9g,\"gradient_x_l2\":%.9g,\"velocity_x_l2\":%.9g,"
           "\"gradient_z_l2\":%.9g,\"velocity_z_l2\":%.9g,\"all_below_one_percent\":%s}\n",
        resolution, sqrt(history_error[0] / history_scale[0]),
        sqrt(history_error[1] / history_scale[1]), sqrt(history_error[2] / history_scale[2]),
        sqrt(history_error[3] / history_scale[3]), sqrt(history_error[4] / history_scale[4]),
        sqrt(history_error[5] / history_scale[5]), accurate ? "true" : "false");
    /* Six complete gameplay queries and a 16x16 detail surface. Timings include
     * rebuilding the shared spectral cache; exclude reference and diagnostics. */
    double times[11], components[4][11], checksum = 0;
    for (unsigned trial = 0; trial < 12; ++trial) {
        double begin = seconds();
        prepare(&p, 2880 + trial);
        double prepared = seconds();
        for (unsigned j = 0; j < 6; ++j)
            checksum += norm(query(&p, .1 + j, .2));
        double queried = seconds();
        for (unsigned z = 0; z < 16; ++z)
            for (unsigned x = 0; x < 16; ++x)
                checksum += height_at(&p, (double)x - 8, (double)z - 8);
        double surface = seconds();
        checksum += energy(&p) * 1e-6;
        double audited = seconds();
        if (trial) {
            times[trial - 1] = 1000 * (audited - begin);
            components[0][trial - 1] = 1000 * (prepared - begin);
            components[1][trial - 1] = 1000 * (queried - prepared);
            components[2][trial - 1] = 1000 * (surface - queried);
            components[3][trial - 1] = 1000 * (audited - surface);
        }
    }
    qsort(times, 11, sizeof(*times), compare_double);
    for (unsigned j = 0; j < 4; ++j)
        qsort(components[j], 11, sizeof(double), compare_double);
    printf("{\"kind\":\"cost\",\"resolution\":%u,\"nodes\":%zu,\"packets\":%zu,"
           "\"owned_bytes\":%zu,\"median_ms\":%.6f,\"p95_ms\":%.6f,\"max_ms\":%.6f,"
           "\"prepare_ms\":%.6f,\"six_queries_ms\":%.6f,\"surface_ms\":%.6f,"
           "\"audit_ms\":%.6f,\"checksum\":%.9g,\"reference_bytes\":%zu}\n",
        resolution, p.nodes, p.count, sizeof(p) + p.nodes * sizeof(*p.node), times[5], times[10],
        times[10], components[0][5], components[1][5], components[2][5], components[3][5], checksum,
        boat_wake_storage_bytes(reference));
    if (csv)
        fclose(csv);
    free(p.node);
    free(impulse);
    boat_wake_destroy(reference);
}

int main(int argc, char **argv)
{
    checks();
    reference_check();
    if (argc == 2 && strcmp(argv[1], "--check") == 0)
        return 0;
    if (argc < 2 || argc > 4) {
        fprintf(
            stderr, "usage: wake_packet_review --check | 32|64|128|256 [--double] [capture.csv]\n");
        return 1;
    }
    unsigned resolution = (unsigned)strtoul(argv[1], NULL, 10);
    REQUIRE(resolution == 32 || resolution == 64 || resolution == 128 || resolution == 256);
    bool doubled = argc >= 3 && strcmp(argv[2], "--double") == 0;
    REQUIRE(argc < 4 || doubled);
    const char *csv = argc == 4 ? argv[3] : argc == 3 && !doubled ? argv[2] : NULL;
    review(resolution, doubled, csv);
    return 0;
}
