#include "boat_wake.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) && !defined(BOAT_WAKE_PORTABLE)
#include <Accelerate/Accelerate.h>
#define WAKE_ACCELERATE 1
#endif

#define WAKE_G 9.81

typedef struct {
    uint32_t first, end;
    int32_t vertical[8];
} WakeRun;

typedef struct {
    double r, i;
} WakeComplex;
typedef struct {
    size_t n;
    WakeComplex *roots, *phase, *input, *output;
#if WAKE_ACCELERATE
    vDSP_DFT_SetupD forward, inverse;
    double *split;
    bool real_transform;
#endif
} WakeTransform;

struct BoatWake {
    BoatWakeConfig config;
    size_t count;
    unsigned char *land;
    double *storage, *eta, *psi, *rate, *op, *a, *b, *c, *damping, *source_op;
    double maximum;
    double *root_coefficients;
    double bound, weights[4];
    unsigned radius;
    uint32_t *neighbors, *edges;
    WakeRun *runs;
    size_t edge_count, run_count;
    BoatWakeLedger ledger;
    double *symbol;
    WakeTransform transform[2];
    bool prepared, has_land;
};

static WakeComplex multiply(WakeComplex a, WakeComplex b)
{
    return (WakeComplex) { a.r * b.r - a.i * b.i, a.r * b.i + a.i * b.r };
}

/* Out-of-place Cooley-Tukey. Every recursion reads disjoint input and writes
 * disjoint output; the small butterflies use roots from the owner plan. */
static void transform(const WakeTransform *p, const WakeComplex *in, WakeComplex *out, size_t n,
    size_t stride, bool inverse)
{
    if (n == 1) {
        *out = *in;
        return;
    }
    size_t radix = n % 2 == 0 ? 2 : 3, m = n / radix;
    for (size_t j = 0; j < radix; ++j)
        transform(p, in + j * stride, out + j * m, m, stride * radix, inverse);
    for (size_t k = 0; k < m; ++k) {
        WakeComplex v[3];
        for (size_t j = 0; j < radix; ++j)
            v[j] = out[j * m + k];
        for (size_t l = 0; l < radix; ++l) {
            WakeComplex sum = v[0];
            for (size_t j = 1; j < radix; ++j) {
                WakeComplex root = p->roots[(j * (k + l * m) * stride) % p->n];
                if (inverse)
                    root.i = -root.i;
                WakeComplex term = multiply(v[j], root);
                sum.r += term.r;
                sum.i += term.i;
            }
            out[l * m + k] = sum;
        }
    }
}

static void cosine_line(WakeTransform *p, double *values, size_t stride, bool inverse)
{
    size_t n = p->n;
#if WAKE_ACCELERATE
    if (p->split && p->real_transform) {
        /* The reordered DCT input is real. Pack only its independent half spectrum,
         * retaining double precision and the same cosine normalization. vDSP's
         * real forward DFT has an extra factor of two; its inverse scales by N. */
        double *real = p->split, *imag = real + n, *out_real = imag + n, *out_imag = out_real + n;
        if (!inverse) {
            for (size_t i = 0; i < n / 4; ++i) {
                real[i] = values[4 * i * stride];
                imag[i] = values[(4 * i + 2) * stride];
                real[n / 2 - 1 - i] = values[(4 * i + 3) * stride];
                imag[n / 2 - 1 - i] = values[(4 * i + 1) * stride];
            }
            vDSP_DFT_ExecuteD(p->forward, real, imag, out_real, out_imag);
            values[0] = .5 * out_real[0];
            values[n / 2 * stride] = .5 * out_imag[0] * p->phase[n / 2].r;
            for (size_t k = 1; k < n / 2; ++k) {
                values[k * stride]
                    = .5 * (out_real[k] * p->phase[k].r - out_imag[k] * p->phase[k].i);
                values[(n - k) * stride]
                    = .5 * (out_real[k] * p->phase[n - k].r + out_imag[k] * p->phase[n - k].i);
            }
        } else {
            real[0] = values[0];
            imag[0] = values[n / 2 * stride] / p->phase[n / 2].r;
            for (size_t k = 1; k < n / 2; ++k) {
                WakeComplex phase = p->phase[k];
                phase.i = -phase.i;
                WakeComplex v = multiply(
                    (WakeComplex) { values[k * stride], -values[(n - k) * stride] }, phase);
                real[k] = v.r;
                imag[k] = v.i;
            }
            vDSP_DFT_ExecuteD(p->inverse, real, imag, out_real, out_imag);
            for (size_t i = 0; i < n / 4; ++i) {
                values[4 * i * stride] = out_real[i] / n;
                values[(4 * i + 2) * stride] = out_imag[i] / n;
                values[(4 * i + 3) * stride] = out_real[n / 2 - 1 - i] / n;
                values[(4 * i + 1) * stride] = out_imag[n / 2 - 1 - i] / n;
            }
        }
        return;
    }
    if (p->split) {
        /* Use the native split buffers directly. Instruments identified the
         * redundant interleaved input/output copies as a substantial cost. */
        double *real = p->split, *imag = real + n, *out_real = imag + n, *out_imag = out_real + n;
        if (!inverse) {
            for (size_t i = 0; i < n / 2; ++i) {
                real[i] = values[2 * i * stride];
                real[n - 1 - i] = values[(2 * i + 1) * stride];
            }
            memset(imag, 0, n * sizeof(double));
        } else {
            real[0] = values[0];
            imag[0] = 0;
            for (size_t k = 1; k < n; ++k) {
                WakeComplex phase = p->phase[k];
                phase.i = -phase.i;
                WakeComplex v = multiply(
                    (WakeComplex) { values[k * stride], -values[(n - k) * stride] }, phase);
                real[k] = v.r;
                imag[k] = v.i;
            }
        }
        vDSP_DFT_ExecuteD(inverse ? p->inverse : p->forward, real, imag, out_real, out_imag);
        if (!inverse) {
            for (size_t k = 0; k < n; ++k)
                values[k * stride] = out_real[k] * p->phase[k].r - out_imag[k] * p->phase[k].i;
        } else {
            for (size_t i = 0; i < n / 2; ++i) {
                values[2 * i * stride] = out_real[i] / n;
                values[(2 * i + 1) * stride] = out_real[n - 1 - i] / n;
            }
        }
        return;
    }
#endif
    if (!inverse) {
        for (size_t i = 0; i < n / 2; ++i) {
            p->input[i] = (WakeComplex) { values[2 * i * stride], 0 };
            p->input[n - 1 - i] = (WakeComplex) { values[(2 * i + 1) * stride], 0 };
        }
        /* Real reordering reduces the DCT to a length-N complex FFT. */
    } else {
        p->input[0] = (WakeComplex) { values[0], 0 };
        for (size_t k = 1; k < n; ++k) {
            WakeComplex phase = p->phase[k];
            phase.i = -phase.i;
            p->input[k]
                = multiply((WakeComplex) { values[k * stride], -values[(n - k) * stride] }, phase);
        }
    }
    transform(p, p->input, p->output, n, 1, inverse);
    if (!inverse) {
        for (size_t k = 0; k < n; ++k)
            values[k * stride] = multiply(p->output[k], p->phase[k]).r;
    } else {
        for (size_t i = 0; i < n / 2; ++i) {
            values[2 * i * stride] = p->output[i].r / n;
            values[(2 * i + 1) * stride] = p->output[n - 1 - i].r / n;
        }
    }
}

static void transpose(const double *input, double *output, size_t width, size_t height)
{
    /* Small blocks keep both sides in cache, including power-of-two row strides. */
    for (size_t z0 = 0; z0 < height; z0 += 16)
        for (size_t x0 = 0; x0 < width; x0 += 16) {
            size_t end_z = z0 + 16 < height ? z0 + 16 : height;
            size_t end_x = x0 + 16 < width ? x0 + 16 : width;
            for (size_t z = z0; z < end_z; ++z)
                for (size_t x = x0; x < end_x; ++x)
                    output[x * height + z] = input[z * width + x];
        }
}

/* Leave the spectrum transposed in existing graph scratch. Contiguous transforms
 * avoid repeatedly gathering/scattering whole columns through a large row stride. */
static void cosine_forward(BoatWake *f, const double *input, double *work)
{
    size_t w = f->config.width, h = f->config.height;
    memcpy(work, input, f->count * sizeof(double));
    for (size_t z = 0; z < h; ++z)
        cosine_line(&f->transform[0], work + z * w, 1, false);
    transpose(work, f->b, w, h);
    for (size_t x = 0; x < w; ++x)
        cosine_line(&f->transform[1], f->b + x * h, 1, false);
}

static void apply_cosine(BoatWake *f, const double *input, double *output)
{
    size_t w = f->config.width, h = f->config.height;
    cosine_forward(f, input, output);
    for (size_t x = 0; x < w; ++x)
        for (size_t z = 0; z < h; ++z)
            f->b[x * h + z] *= f->symbol[z * w + x];
    for (size_t x = 0; x < w; ++x)
        cosine_line(&f->transform[1], f->b + x * h, 1, true);
    transpose(f->b, output, h, w);
    for (size_t z = 0; z < h; ++z)
        cosine_line(&f->transform[0], output + z * w, 1, true);
}

/* Parseval for this unnormalized DCT: inverse norms are 1/N for mode zero,
 * 2/N otherwise. Source self-energy needs only the forward transform. */
static double cosine_quadratic(BoatWake *f, const double *input)
{
    size_t w = f->config.width, h = f->config.height;
    cosine_forward(f, input, f->source_op);
    double result = 0;
    for (size_t z = 0; z < h; ++z)
        for (size_t x = 0; x < w; ++x) {
            size_t i = z * w + x;
            double v = f->b[x * h + z];
            result += (x ? 2 : 1) * (z ? 2 : 1) * v * v * f->symbol[i] / f->count;
        }
    return result;
}

bool boat_wake_use_cosine(BoatWake *f)
{
    if (!f)
        return false;
    if (f->symbol)
        return true;
    for (size_t i = 0; i < f->count; ++i)
        if (f->land[i])
            return false;
    for (size_t axis = 0; axis < 2; ++axis) {
        size_t n = axis ? f->config.height : f->config.width;
        if (n % 2)
            return false;
        while (n % 2 == 0)
            n /= 2;
        while (n % 3 == 0)
            n /= 3;
        if (n != 1)
            return false;
    }
    double *symbol = malloc(f->count * sizeof(double));
    WakeComplex *a = calloc(4 * f->config.width, sizeof(WakeComplex));
    WakeComplex *b = calloc(4 * f->config.height, sizeof(WakeComplex));
    if (!symbol || !a || !b) {
        free(symbol);
        free(a);
        free(b);
        return false;
    }
    f->symbol = symbol;
    const double pi = 3.14159265358979323846;
    for (size_t axis = 0; axis < 2; ++axis) {
        size_t n = axis ? f->config.height : f->config.width;
        WakeComplex *v = axis ? b : a;
        f->transform[axis] = (WakeTransform) {
            .n = n, .roots = v, .phase = v + n, .input = v + 2 * n, .output = v + 3 * n
        };
#if WAKE_ACCELERATE
        WakeTransform *p = &f->transform[axis];
        if (n % 16 == 0) {
            p->forward = vDSP_DFT_zrop_CreateSetupD(NULL, n, vDSP_DFT_FORWARD);
            p->inverse = vDSP_DFT_zrop_CreateSetupD(NULL, n, vDSP_DFT_INVERSE);
        }
        p->real_transform = p->forward && p->inverse;
        if (!p->real_transform) {
            if (p->forward)
                vDSP_DFT_DestroySetupD(p->forward);
            if (p->inverse)
                vDSP_DFT_DestroySetupD(p->inverse);
            p->forward = vDSP_DFT_zop_CreateSetupD(NULL, n, vDSP_DFT_FORWARD);
            p->inverse = vDSP_DFT_zop_CreateSetupD(NULL, n, vDSP_DFT_INVERSE);
        }
        if (p->forward && p->inverse)
            p->split = calloc(4 * n, sizeof(double));
#endif
        for (size_t k = 0; k < n; ++k) {
            v[k] = (WakeComplex) { cos(2 * pi * k / n), -sin(2 * pi * k / n) };
            v[n + k] = (WakeComplex) { cos(pi * k / (2 * n)), -sin(pi * k / (2 * n)) };
        }
    }
    for (size_t z = 0; z < f->config.height; ++z)
        for (size_t x = 0; x < f->config.width; ++x)
            symbol[z * f->config.width + x]
                = boat_wake_operator_symbol(f->config.order, f->config.spatial_order,
                    f->config.spacing, pi * x / (f->config.width * f->config.spacing),
                    pi * z / (f->config.height * f->config.spacing));
    return true;
}

/* Water-only graph Laplacian L, with no periodic edges. Approximate sqrt(L) by
 * sum_j 4 sqrt(a)/(pi(4j^2-1)) [I-T_j(I-2L/a)], a bounds the chosen stencil.
 * Close the omitted tail with a normalized Fejer kernel: add C(1-F_p(B)),
 * C=2 sqrt(a)/(pi(2p+1)). Its extra positive weights vanish at the constant mode.
 * Each term is positive semidefinite and kills constants. Polynomial paths only
 * follow water edges: disconnected components cannot communicate through land. */
static void apply_reference(BoatWake *f, const double *input, double *output)
{
    size_t w = f->config.width, h = f->config.height;
    memcpy(f->a, input, f->count * sizeof(double));
    memcpy(f->c, input, f->count * sizeof(double));
    memset(output, 0, f->count * sizeof(double));
    double scale = 4 * sqrt(f->bound) / (3.14159265358979323846 * f->config.spacing);
    double tail = scale / (2 * (2.0 * f->config.order + 1));
    for (unsigned j = 1; j <= f->config.order; ++j) {
        double fejer
            = 2.0 * (f->config.order + 1 - j) / ((f->config.order + 1.0) * (f->config.order + 1.0));
        double weight = scale / (4.0 * j * j - 1) + tail * fejer;
        for (size_t z = 0; z < h; ++z) {
            for (size_t x = 0; x < w; ++x) {
                size_t i = z * w + x;
                double v = f->a[i];
                if (f->land[i]) {
                    f->b[i] = 0;
                    continue;
                }
                double lap = 0;
                if (f->radius == 1) {
                    if (x && !f->land[i - 1])
                        lap += v - f->a[i - 1];
                    if (x + 1 < w && !f->land[i + 1])
                        lap += v - f->a[i + 1];
                    if (z && !f->land[i - w])
                        lap += v - f->a[i - w];
                    if (z + 1 < h && !f->land[i + w])
                        lap += v - f->a[i + w];
                } else {
                    for (unsigned r = 0; r < f->radius; ++r) {
                        const uint32_t *neighbors = f->neighbors + (i * f->radius + r) * 4;
                        double sum = 0;
                        for (unsigned d = 0; d < 4; ++d)
                            sum += v - f->a[neighbors[d]];
                        lap += f->weights[r] * sum;
                    }
                }
                double bv = v - 2 * lap / f->bound;
                f->b[i] = j == 1 ? bv : 2 * bv - f->c[i];
                output[i] += weight * (input[i] - f->b[i]);
            }
        }
        double *swap = f->c;
        f->c = f->a;
        f->a = f->b;
        f->b = swap;
    }
}

/* All input/scratch/output fields are disjoint owner allocations/slices. Runs have
 * straight horizontal water stencils and shared mirrored vertical offsets. Remaining
 * coast/end cells use compact scalar neighbor maps. */
static inline double differences(const double *a, size_t i, size_t r, const int32_t *vertical)
{
    double v = a[i];
    return 4 * v
        - ((a[i - r] + a[i + r])
            + (a[(ptrdiff_t)i + vertical[2 * (r - 1)]]
                + a[(ptrdiff_t)i + vertical[2 * (r - 1) + 1]]));
}

/* Clenshaw evaluates R(B), where A=(I-B)R(B). Factoring out I-B keeps
 * constants exactly zero and avoids updating an output accumulator at every degree.
 * R_0=sum(j*w_j); R_k=2*sum((j-k)*w_j), j>k, for the same positive weights. */
static void regular8(const double *restrict a, const double *restrict previous,
    const double *restrict input, double *restrict next, size_t first, size_t end,
    const int32_t *vertical, double recurrence, double coefficient, double factor)
{
#if defined(__clang__)
#pragma clang loop interleave_count(4)
#endif
    for (size_t i = first; i < end; ++i) {
        double near = (8.0 / 5) * differences(a, i, 1, vertical)
            - (1.0 / 5) * differences(a, i, 2, vertical);
        double far = (8.0 / 315) * differences(a, i, 3, vertical)
            - (1.0 / 560) * differences(a, i, 4, vertical);
        double lap = near + far;
        double value = recurrence * a[i] - previous[i];
        value += coefficient * input[i];
        value -= recurrence * factor * lap;
        next[i] = value;
    }
}

static void regular4(const double *restrict a, const double *restrict previous,
    const double *restrict input, double *restrict next, size_t first, size_t end,
    const int32_t *vertical, double recurrence, double coefficient, double factor)
{
#if defined(__clang__)
#pragma clang loop interleave_count(4)
#endif
    for (size_t i = first; i < end; ++i) {
        double lap = (4.0 / 3) * differences(a, i, 1, vertical)
            - (1.0 / 12) * differences(a, i, 2, vertical);
        double value = recurrence * a[i] - previous[i];
        value += coefficient * input[i];
        value -= recurrence * factor * lap;
        next[i] = value;
    }
}

static void recurrence(BoatWake *f, const double *input, double coefficient, double scale)
{
    double factor = 2 / f->bound;
    for (size_t r = 0; r < f->run_count; ++r) {
        WakeRun run = f->runs[r];
        if (f->radius == 4)
            regular8(f->a, f->c, input, f->b, run.first, run.end, run.vertical, scale, coefficient,
                factor);
        else
            regular4(f->a, f->c, input, f->b, run.first, run.end, run.vertical, scale, coefficient,
                factor);
    }
    for (size_t e = 0; e < f->edge_count; ++e) {
        size_t i = f->edges[e];
        double v = f->a[i], lap = 0;
        for (unsigned r = 0; r < f->radius; ++r) {
            const uint32_t *n = f->neighbors + (e * f->radius + r) * 4;
            double sum = 0;
            for (unsigned d = 0; d < 4; ++d)
                sum += v - f->a[n[d]];
            lap += f->weights[r] * sum;
        }
        double value = scale * v - f->c[i];
        value += coefficient * input[i];
        value -= scale * factor * lap;
        f->b[i] = value;
    }
}

static void finish(BoatWake *f, const double *input, double *output)
{
    double factor = 2 / f->bound;
    for (size_t r = 0; r < f->run_count; ++r) {
        WakeRun run = f->runs[r];
        if (f->radius == 4) {
            for (size_t i = run.first; i < run.end; ++i) {
                double near = (8.0 / 5) * differences(input, i, 1, run.vertical)
                    - (1.0 / 5) * differences(input, i, 2, run.vertical);
                double far = (8.0 / 315) * differences(input, i, 3, run.vertical)
                    - (1.0 / 560) * differences(input, i, 4, run.vertical);
                output[i] = factor * (near + far);
            }
        } else {
            for (size_t i = run.first; i < run.end; ++i)
                output[i] = factor
                    * ((4.0 / 3) * differences(input, i, 1, run.vertical)
                        - (1.0 / 12) * differences(input, i, 2, run.vertical));
        }
    }
    for (size_t e = 0; e < f->edge_count; ++e) {
        size_t i = f->edges[e];
        double v = input[i], lap = 0;
        for (unsigned r = 0; r < f->radius; ++r) {
            const uint32_t *n = f->neighbors + (e * f->radius + r) * 4;
            double sum = 0;
            for (unsigned d = 0; d < 4; ++d)
                sum += v - input[n[d]];
            lap += f->weights[r] * sum;
        }
        output[i] = factor * lap;
    }
}

static void apply(BoatWake *f, const double *input, double *output)
{
    if (f->symbol) {
        apply_cosine(f, input, output);
        return;
    }
    if (f->radius == 1 || f->config.reference_operator) {
        apply_reference(f, input, output);
        return;
    }
    unsigned degree = f->config.order - 1;
    for (size_t i = 0; i < f->count; ++i)
        f->a[i] = f->root_coefficients[degree] * input[i];
    memset(f->c, 0, f->count * sizeof(double));
    memset(output, 0, f->count * sizeof(double));
    if (!degree) {
        finish(f, f->a, output);
        return;
    }
    for (unsigned j = degree - 1; j > 0; --j) {
        recurrence(f, input, f->root_coefficients[j], 2);
        double *swap = f->c;
        f->c = f->a;
        f->a = f->b;
        f->b = swap;
    }
    recurrence(f, input, f->root_coefficients[0], 1);
    finish(f, f->b, output);
}

static unsigned spatial_weights(unsigned order, double weights[4])
{
    if (order == 8) {
        weights[0] = 8.0 / 5;
        weights[1] = -1.0 / 5;
        weights[2] = 8.0 / 315;
        weights[3] = -1.0 / 560;
        return 4;
    }
    if (order == 4) {
        weights[0] = 4.0 / 3;
        weights[1] = -1.0 / 12;
        return 2;
    }
    weights[0] = 1;
    return 1;
}

/* Mirror within each contiguous water segment, never across its land endpoints.
 * These stencils equal positive powers of that segment's Neumann graph Laplacian. */
static void build_neighbors(BoatWake *f)
{
    for (unsigned axis = 0; axis < 2; ++axis) {
        size_t length = axis ? f->config.height : f->config.width;
        size_t lines = axis ? f->config.width : f->config.height;
        size_t stride = axis ? f->config.width : 1;
        for (size_t line = 0; line < lines; ++line) {
            size_t base = axis ? line : line * f->config.width;
            size_t start = 0;
            while (start < length) {
                if (f->land[base + start * stride]) {
                    ++start;
                    continue;
                }
                size_t end = start + 1;
                while (end < length && !f->land[base + end * stride])
                    ++end;
                int count = (int)(end - start);
                for (size_t pos = start; pos < end; ++pos) {
                    size_t i = base + pos * stride;
                    for (unsigned r = 0; r < f->radius; ++r) {
                        for (unsigned sign = 0; sign < 2; ++sign) {
                            int offset = (int)(pos - start) + (sign ? 1 : -1) * (int)(r + 1);
                            offset = ((offset % (2 * count)) + 2 * count) % (2 * count);
                            if (offset >= count)
                                offset = 2 * count - 1 - offset;
                            f->neighbors[(i * f->radius + r) * 4 + 2 * axis + sign]
                                = (uint32_t)(base + (start + (size_t)offset) * stride);
                        }
                    }
                }
                start = end;
            }
        }
    }
}

static bool same_vertical(const BoatWake *f, size_t i, size_t j)
{
    for (unsigned r = 0; r < f->radius; ++r) {
        const uint32_t *a = f->neighbors + (i * f->radius + r) * 4;
        const uint32_t *b = f->neighbors + (j * f->radius + r) * 4;
        if ((int64_t)a[2] - (int64_t)i != (int64_t)b[2] - (int64_t)j
            || (int64_t)a[3] - (int64_t)i != (int64_t)b[3] - (int64_t)j)
            return false;
    }
    return true;
}

static bool partition_water(BoatWake *f)
{
    size_t w = f->config.width, h = f->config.height;
    unsigned char *regular = calloc(f->count, 1);
    if (!regular)
        return false;
    for (size_t z = 0; z < h; ++z)
        for (size_t x = 0; x < w; ++x) {
            size_t i = z * w + x;
            if (f->land[i])
                continue;
            bool fast = x >= f->radius && x + f->radius < w;
            for (unsigned r = 0; fast && r < f->radius; ++r) {
                const uint32_t *n = f->neighbors + (i * f->radius + r) * 4;
                size_t d = r + 1;
                fast = n[0] == i - d && n[1] == i + d;
            }
            regular[i] = fast;
            if (!fast)
                ++f->edge_count;
            if (fast && (!x || !regular[i - 1] || !same_vertical(f, i, i - 1)))
                ++f->run_count;
        }
    f->runs = calloc(f->run_count ? f->run_count : 1, sizeof(*f->runs));
    f->edges = calloc(f->edge_count ? f->edge_count : 1, sizeof(*f->edges));
    uint32_t *compact = calloc(f->edge_count ? f->edge_count * f->radius * 4 : 1, sizeof(*compact));
    if (!f->runs || !f->edges || !compact) {
        free(regular);
        free(compact);
        return false;
    }
    size_t e = 0, r = 0;
    for (size_t z = 0; z < h; ++z) {
        for (size_t x = 0; x < w; ++x) {
            size_t i = z * w + x;
            if (regular[i]) {
                size_t first = i;
                while (x + 1 < w && regular[i + 1] && same_vertical(f, first, i + 1)) {
                    ++x;
                    ++i;
                }
                WakeRun *run = &f->runs[r++];
                run->first = (uint32_t)first;
                run->end = (uint32_t)(i + 1);
                for (unsigned k = 0; k < f->radius; ++k) {
                    const uint32_t *n = f->neighbors + (first * f->radius + k) * 4;
                    run->vertical[2 * k] = (int32_t)((int64_t)n[2] - (int64_t)first);
                    run->vertical[2 * k + 1] = (int32_t)((int64_t)n[3] - (int64_t)first);
                }
            } else if (!f->land[i]) {
                f->edges[e] = (uint32_t)i;
                memcpy(compact + e * f->radius * 4, f->neighbors + i * f->radius * 4,
                    f->radius * 4 * sizeof(*compact));
                ++e;
            }
        }
    }
    free(regular);
    free(f->neighbors);
    f->neighbors = compact;
    return true;
}

static double energy(BoatWake *f)
{
    boat_wake_prepare(f);
    double total = 0;
    for (size_t i = 0; i < f->count; ++i)
        total += WAKE_G * f->eta[i] * f->eta[i] + f->psi[i] * f->op[i];
    return 0.5 * f->config.density * f->config.spacing * f->config.spacing * total;
}

BoatWake *boat_wake_create(const BoatWakeConfig *c)
{
    if (!c || c->width < 2 || c->height < 2 || c->width > 1024 || c->height > 1024
        || !isfinite(c->spacing) || c->spacing <= 0 || !isfinite(c->density) || c->density <= 0
        || !isfinite(c->dt) || c->dt <= 0 || !c->order || c->order > 1024
        || (c->spatial_order && c->spatial_order != 2 && c->spatial_order != 4
            && c->spatial_order != 8))
        return NULL;
    double weights[4] = { 0 };
    unsigned radius = spatial_weights(c->spatial_order, weights);
    double bound = 0;
    for (unsigned r = 0; r < radius; r += 2)
        bound += 8 * weights[r];
    if (c->dt * sqrt(WAKE_G * sqrt(bound) / c->spacing) >= 1)
        return NULL;
    double energy_scale = 0.5 * c->density * c->spacing * c->spacing;
    if (!isfinite(energy_scale) || energy_scale <= 0 || !isfinite(sqrt(8.0) / c->spacing))
        return NULL;
    BoatWake *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->config = *c;
    f->count = c->width * c->height;
    f->radius = radius;
    f->bound = bound;
    if (radius > 1 && !c->reference_operator) {
        f->root_coefficients = calloc(c->order, sizeof(double));
        if (!f->root_coefficients) {
            boat_wake_destroy(f);
            return NULL;
        }
        double scale = 4 * sqrt(bound) / (3.14159265358979323846 * c->spacing);
        double tail = scale / (2 * (2.0 * c->order + 1)), sum = 0, moment = 0;
        for (unsigned j = c->order; j > 0; --j) {
            double fejer = 2.0 * (c->order + 1 - j) / ((c->order + 1.0) * (c->order + 1.0));
            double weight = scale / (4.0 * j * j - 1) + tail * fejer;
            sum += weight;
            moment += j * weight;
            f->root_coefficients[j - 1] = j == 1 ? moment : 2 * (moment - (j - 1) * sum);
        }
    }
    memcpy(f->weights, weights, sizeof(weights));
    /* Bound both raw Hamiltonian products and Clenshaw's intermediate states.
     * Logs avoid overflowing a division before the bound can be applied. */
    double operator_bound = 2 * sqrt(bound) / c->spacing;
    double energy_limit = 0.5
        * (log(DBL_MAX) - log((double)f->count) - log(fmax(WAKE_G, operator_bound))
            - fmax(0, log(energy_scale)));
    double recurrence_limit = log(DBL_MAX) - log(operator_bound) - 3 * log((double)c->order)
        - 0.5 * log((double)f->count);
    f->maximum = 0.001 * exp(fmin(energy_limit, recurrence_limit));
    f->land = calloc(f->count, 1);
    f->storage = calloc(9 * f->count, sizeof(double));
    if (!f->land || !f->storage) {
        boat_wake_destroy(f);
        return NULL;
    }
    f->eta = f->storage;
    f->psi = f->eta + f->count;
    f->rate = f->psi + f->count;
    f->op = f->rate + f->count;
    f->a = f->op + f->count;
    f->b = f->a + f->count;
    f->c = f->b + f->count;
    f->damping = f->c + f->count;
    f->source_op = f->damping + f->count;
    if (c->land)
        memcpy(f->land, c->land, f->count);
    for (size_t i = 0; i < f->count; ++i)
        f->has_land |= f->land[i] != 0;
    if (radius > 1) {
        f->neighbors = calloc(f->count * radius * 4, sizeof(*f->neighbors));
        if (!f->neighbors) {
            boat_wake_destroy(f);
            return NULL;
        }
        build_neighbors(f);
        if (!c->reference_operator && !partition_water(f)) {
            boat_wake_destroy(f);
            return NULL;
        }
    }
    for (size_t i = 0; i < f->count; ++i) {
        double rate = c->absorption ? c->absorption[i] : 0;
        if (!isfinite(rate) || rate < 0) {
            boat_wake_destroy(f);
            return NULL;
        }
        f->rate[i] = rate;
        f->damping[i] = expm1(-rate * c->dt);
    }
    f->config.land = f->land;
    f->config.absorption = f->rate;
    return f;
}

BoatWake *boat_wake_clone(const BoatWake *f)
{
    if (!f)
        return NULL;
    BoatWake *copy = boat_wake_create(&f->config);
    if (copy) {
        if (f->symbol && !boat_wake_use_cosine(copy)) {
            boat_wake_destroy(copy);
            return NULL;
        }
        memcpy(copy->eta, f->eta, 2 * f->count * sizeof(double));
        copy->ledger = f->ledger;
    }
    return copy;
}

void boat_wake_destroy(BoatWake *f)
{
    if (f) {
        free(f->symbol);
        free(f->transform[0].roots);
        free(f->transform[1].roots);
#if WAKE_ACCELERATE
        for (size_t axis = 0; axis < 2; ++axis) {
            if (f->transform[axis].forward)
                vDSP_DFT_DestroySetupD(f->transform[axis].forward);
            if (f->transform[axis].inverse)
                vDSP_DFT_DestroySetupD(f->transform[axis].inverse);
            free(f->transform[axis].split);
        }
#endif
        free(f->root_coefficients);
        free(f->storage);
        free(f->land);
        free(f->neighbors);
        free(f->runs);
        free(f->edges);
        free(f);
    }
}

void boat_wake_reset(BoatWake *f)
{
    if (!f)
        return;
    f->ledger.retired += energy(f);
    memset(f->eta, 0, 2 * f->count * sizeof(double));
    f->ledger.tick = 0;
    f->prepared = false;
}

static bool valid_impulse(const BoatWake *f, const double *impulse, bool *nonzero)
{
    if (!f || !impulse)
        return false;
    *nonzero = false;
    for (size_t i = 0; i < f->count; ++i) {
        if (!isfinite(impulse[i]) || fabs(impulse[i]) > f->maximum
            || fabs(f->psi[i] + impulse[i]) > f->maximum || (f->land[i] && impulse[i] != 0))
            return false;
        *nonzero |= impulse[i] != 0;
    }
    return true;
}

static void book_work(BoatWake *f, double work)
{
    work *= 0.5 * f->config.density * f->config.spacing * f->config.spacing;
    work *= 2;
    f->ledger.injected += work;
    if (work >= 0)
        f->ledger.positive_work += work;
    else
        f->ledger.negative_work += work;
}

static void add_impulse(BoatWake *f, const double *impulse, const double *op, double fraction)
{
    f->prepared = false;
    /* Symmetry of A gives delta H = rho dx^2 (psi + delta/2)^T A delta.
     * One operator instead of subtracting two full Hamiltonians; eta is unchanged.
     * This also avoids cancellation against unrelated, large retained energy. */
    double work = 0;
    for (size_t i = 0; i < f->count; ++i) {
        double delta = fraction * impulse[i];
        work += (f->psi[i] + 0.5 * delta) * (fraction * op[i]);
        f->psi[i] += delta;
    }
    /* Creation validates this half-energy scale; the doubled scale itself can
     * overflow for valid extreme units even when the resulting work is finite. */
    book_work(f, work);
}

bool boat_wake_kick(BoatWake *f, const double *impulse)
{
    bool nonzero;
    if (!valid_impulse(f, impulse, &nonzero))
        return false;
    if (nonzero) {
        apply(f, impulse, f->op);
        add_impulse(f, impulse, f->op, 1);
    }
    return true;
}

bool boat_wake_step_with_impulse(BoatWake *f, const double *impulse)
{
    bool nonzero;
    if (!valid_impulse(f, impulse, &nonzero) || f->ledger.tick == UINT64_MAX)
        return false;
    if (!nonzero)
        return boat_wake_step(f);
    if (f->symbol) {
        /* Reuse the published A psi at both endpoints. The two half events are
         * .5 delta^T A psi_before + .125 delta^T A delta, and
         * .5 delta^T A psi_after  - .125 delta^T A delta. The latter endpoint
         * already includes the second half source. This preserves each signed
         * event while eliminating the inverse source transform. */
        boat_wake_prepare(f);
        double q = cosine_quadratic(f, impulse), cross = 0;
        for (size_t i = 0; i < f->count; ++i) {
            cross += impulse[i] * f->op[i];
            f->psi[i] += .5 * impulse[i];
        }
        book_work(f, .5 * cross + .125 * q);
        boat_wake_step(f);
        for (size_t i = 0; i < f->count; ++i)
            f->psi[i] += .5 * impulse[i];
        boat_wake_prepare(f);
        cross = 0;
        for (size_t i = 0; i < f->count; ++i)
            cross += impulse[i] * f->op[i];
        book_work(f, .5 * cross - .125 * q);
        return true;
    }
    /* Cache A delta across the field step. Two symmetric half sources need one
     * source operator, with each half's actual signed work booked separately. */
    apply(f, impulse, f->source_op);
    add_impulse(f, impulse, f->source_op, .5);
    boat_wake_step(f);
    add_impulse(f, impulse, f->source_op, .5);
    return true;
}

bool boat_wake_step(BoatWake *f)
{
    if (!f || f->ledger.tick == UINT64_MAX)
        return false;
    double dt = f->config.dt;
    f->prepared = false;
    /* Kick-drift-kick: fixed integer tick, no absolute float phase. */
    for (size_t i = 0; i < f->count; ++i)
        f->psi[i] -= 0.5 * dt * WAKE_G * f->eta[i];
    apply(f, f->psi, f->op);
    for (size_t i = 0; i < f->count; ++i) {
        f->eta[i] += dt * f->op[i];
        f->psi[i] -= 0.5 * dt * WAKE_G * f->eta[i];
    }
    /* Damping eta alone is a provably nonnegative Hamiltonian sink, even near land.
     * A spatial mask on psi could increase its nonlocal kinetic energy. */
    double lost = 0;
    for (size_t i = 0; i < f->count; ++i) {
        double delta = f->damping[i];
        if (delta == 0)
            continue;
        double old = f->eta[i];
        f->eta[i] = old * (1 + delta);
        lost += old * old * (-delta) * (2 + delta);
    }
    f->ledger.absorbed
        += (0.5 * f->config.density * f->config.spacing * f->config.spacing) * (WAKE_G * lost);
    ++f->ledger.tick;
    return true;
}

bool boat_wake_sample(const BoatWake *f, size_t x, size_t z, double *eta, double *psi)
{
    if (!f || x >= f->config.width || z >= f->config.height || !eta || !psi)
        return false;
    *eta = f->eta[z * f->config.width + x];
    *psi = f->psi[z * f->config.width + x];
    return true;
}

void boat_wake_prepare(BoatWake *f)
{
    if (f && !f->prepared) {
        apply(f, f->psi, f->op);
        f->prepared = true;
    }
}

static void interpolation(double t, double *w, double *d)
{
    static const double inverse[12]
        = { -2.505210838544172e-08, 2.7557319223985888e-07, -1.3778659611992946e-06,
              4.1335978835978839e-06, -8.2671957671957678e-06, 1.1574074074074073e-05,
              -1.1574074074074073e-05, 8.2671957671957678e-06, -4.1335978835978839e-06,
              1.3778659611992946e-06, -2.7557319223985888e-07, 2.505210838544172e-08 };
    double prefix[13], suffix[13], dp[13], ds[13];
    prefix[0] = suffix[12] = 1;
    dp[0] = ds[12] = 0;
    for (int k = 0; k < 12; ++k) {
        double v = t - (k - 5);
        prefix[k + 1] = prefix[k] * v;
        dp[k + 1] = dp[k] * v + prefix[k];
        int j = 11 - k;
        v = t - (j - 5);
        suffix[j] = suffix[j + 1] * v;
        ds[j] = ds[j + 1] * v + suffix[j + 1];
    }
    for (int k = 0; k < 12; ++k) {
        w[k] = prefix[k] * suffix[k + 1] * inverse[k];
        d[k] = (dp[k] * suffix[k + 1] + prefix[k] * ds[k + 1]) * inverse[k];
    }
}

static size_t mirrored(int i, size_t n)
{
    int m = (int)n;
    i = ((i % (2 * m)) + 2 * m) % (2 * m);
    return (size_t)(i >= m ? 2 * m - 1 - i : i);
}

bool boat_wake_query(const BoatWake *f, double x, double z, BoatWakeSample *q)
{
    if (!f || !q || !f->prepared || !isfinite(x) || !isfinite(z))
        return false;
    *q = (BoatWakeSample) { 0 };
    double dx = f->config.spacing;
    x = x / dx + f->config.width * .5 - .5;
    z = z / dx + f->config.height * .5 - .5;
    if (x < -.5 || z < -.5 || x > f->config.width - .5 || z > f->config.height - .5)
        return false;
    int ix = (int)floor(x), iz = (int)floor(z);
    /* Do not reconstruct through a thin island. A shoreline interpolation scheme
     * needs separate validation; the playable rectangle never takes this branch. */
    if (f->has_land)
        for (int j = 0; j < 12; ++j)
            for (int k = 0; k < 12; ++k)
                if (f->land[mirrored(iz + j - 5, f->config.height) * f->config.width
                        + mirrored(ix + k - 5, f->config.width)])
                    return false;
    double wx[12], wz[12], gx[12], gz[12];
    interpolation(x - ix, wx, gx);
    interpolation(z - iz, wz, gz);
    for (int j = 0; j < 12; ++j) {
        size_t row = mirrored(iz + j - 5, f->config.height) * f->config.width;
        for (int k = 0; k < 12; ++k) {
            size_t i = row + mirrored(ix + k - 5, f->config.width);
            double w = wx[k] * wz[j], a = gx[k] * wz[j] / dx, b = wx[k] * gz[j] / dx;
            q->height += w * f->eta[i];
            q->gradient_x += a * f->eta[i];
            q->gradient_z += b * f->eta[i];
            q->velocity_x += a * f->psi[i];
            q->velocity_z += b * f->psi[i];
            q->vertical_velocity += w * (f->op[i] - f->rate[i] * f->eta[i]);
        }
    }
    return true;
}

BoatWakeConfig boat_wake_config(const BoatWake *f)
{
    return f ? f->config : (BoatWakeConfig) { 0 };
}

void boat_wake_copy_heights(const BoatWake *f, float *out)
{
    if (f && out)
        for (size_t i = 0; i < f->count; ++i)
            out[i] = (float)f->eta[i];
}

void boat_wake_extrema(const BoatWake *f, double *height, double *slope)
{
    if (!f || !height || !slope)
        return;
    *height = *slope = 0;
    double weights[12], derivative[12];
    interpolation(0, weights, derivative);
    for (size_t z = 0; z < f->config.height; ++z)
        for (size_t x = 0; x < f->config.width; ++x) {
            size_t i = z * f->config.width + x;
            double gx = 0, gz = 0;
            for (int k = 0; k < 12; ++k) {
                gx += derivative[k]
                    * f->eta[z * f->config.width + mirrored((int)x + k - 5, f->config.width)];
                gz += derivative[k]
                    * f->eta[mirrored((int)z + k - 5, f->config.height) * f->config.width + x];
            }
            *height = fmax(*height, fabs(f->eta[i]));
            *slope = fmax(*slope, hypot(gx, gz) / f->config.spacing);
        }
}

BoatWakeLedger boat_wake_ledger(BoatWake *f)
{
    if (!f)
        return (BoatWakeLedger) { 0 };
    BoatWakeLedger result = f->ledger;
    result.retained = energy(f);
    /* Work and sinks are accumulated independently. Audit the actual Hamiltonian
     * on demand, so normal ticks do not spend two extra operators measuring drift. */
    result.numerical_residual
        = result.retained + result.absorbed + result.retired - result.injected;
    return result;
}

size_t boat_wake_storage_bytes(const BoatWake *f)
{
    if (!f)
        return 0;
    size_t bytes = sizeof(*f) + f->count * (9 * sizeof(double) + 1);
    if (f->symbol)
        bytes += f->count * sizeof(double)
            + 4 * (f->config.width + f->config.height) * sizeof(WakeComplex);
#if WAKE_ACCELERATE
    for (size_t axis = 0; axis < 2; ++axis)
        if (f->transform[axis].split)
            bytes += 4 * f->transform[axis].n * sizeof(double);
#endif
    if (f->root_coefficients)
        bytes += f->config.order * sizeof(double);
    if (f->neighbors) {
        size_t count = f->config.reference_operator ? f->count * f->radius * 4
                                                    : f->edge_count * f->radius * 4;
        bytes += (count ? count : 1) * sizeof(uint32_t);
    }
    if (f->edges)
        bytes += (f->edge_count ? f->edge_count : 1) * sizeof(uint32_t);
    if (f->runs)
        bytes += (f->run_count ? f->run_count : 1) * sizeof(WakeRun);
    return bytes;
}

double boat_wake_symbol(unsigned order, double spacing, double kx, double kz)
{
    return boat_wake_operator_symbol(order, 2, spacing, kx, kz);
}

double boat_wake_operator_symbol(
    unsigned order, unsigned spatial_order, double spacing, double kx, double kz)
{
    double weights[4] = { 0 }, bound = 0, lap = 0;
    unsigned radius = spatial_weights(spatial_order, weights);
    for (unsigned r = 0; r < radius; ++r) {
        lap += 2 * weights[r] * (2 - cos((r + 1) * kx * spacing) - cos((r + 1) * kz * spacing));
        if (!(r % 2))
            bound += 8 * weights[r];
    }
    double b = 1 - 2 * lap / bound;
    double previous = 1, current = b, sum = 0;
    double tail = 1 / (2 * (2.0 * order + 1));
    for (unsigned j = 1; j <= order; ++j) {
        double fejer = 2.0 * (order + 1 - j) / ((order + 1.0) * (order + 1.0));
        sum += (1 - current) * (1 / (4.0 * j * j - 1) + tail * fejer);
        double next = 2 * b * current - previous;
        previous = current;
        current = next;
    }
    return 4 * sqrt(bound) / (3.14159265358979323846 * spacing) * sum;
}
