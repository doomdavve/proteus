#!/usr/bin/env python3
"""R2b scalar algebra audits, not a wake evaluator or production prototype.

The signed synthetic reference below has finite longitudinal support and birth/fade gates.
It tests transport identities and chain rules, not production wave parity or gameplay.
"""
import argparse
import csv
import hashlib
import json
import math
import platform
import sys
from pathlib import Path

C = 2.4


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def dilation(v, curvature, epsilon):
    x = -curvature * v
    if x <= 0:
        return 1.0, 0.0
    denominator = epsilon * epsilon + x * x
    f = x * x * x / denominator
    derivative = x * x * (3 * epsilon * epsilon + x * x) / (denominator * denominator)
    return 1 + f, -curvature * derivative


def smooth(x):
    x = max(0, min(1, x))
    return x * x * (3 - 2 * x), 6 * x * (1 - x)


def reference(s, v, t, length=7.5, inverse_speed=1/15, born=0, amplitude=1.2):
    """Synthetic signed profile: sin(q)*exp(-q*q/10), NOT the production recipe."""
    clock = t - born
    if abs(s) >= length or clock < 0 or clock >= 16:
        return (0.0,) * 4
    age = clock - inverse_speed * s
    birth, db = smooth(clock / .3)
    rise, dr = smooth(age / .65)
    fall, df = smooth((age - (12 - length * inverse_speed)) / 4)
    fall = 1 - fall
    envelope = birth * rise * fall
    ea = birth * (dr / .65 * fall - rise * df / 4)
    et = db / .3 * rise * fall + ea
    angle = math.pi * s / (2 * length)
    cosine = math.cos(angle)
    window = cosine * cosine
    ws = -math.pi / length * cosine * math.sin(angle)
    q = v - C * age
    exponential = math.exp(-q * q / 10)
    shape = math.sin(q) * exponential
    dq = (math.cos(q) - q / 5 * math.sin(q)) * exponential
    h = amplitude * window * envelope * shape
    hs = amplitude * ((ws * envelope - window * inverse_speed * ea) * shape
                      + window * envelope * dq * C * inverse_speed)
    hv = amplitude * window * envelope * dq
    ht = amplitude * window * (et * shape - C * envelope * dq)
    return h, hs, hv, ht


def transported(u, v, t, curvature, epsilon, exponent=1, **parameters):
    d, dv = dilation(v, curvature, epsilon)
    s = u / d
    h, hs, hv, ht = reference(s, v, t, **parameters)
    multiplier = d ** -exponent
    return (multiplier * h, multiplier * hs / d,
            multiplier * (hv - dv / d * (s * hs + exponent * h)), multiplier * ht)


def phase_map(s, q, t, curvature, epsilon, side):
    r = .9 + C * (t - s / 15) + q
    require(r > .5, 'phase chart outside real regularized radius')
    v = side * math.sqrt(r * r - .25)
    d, dv = dilation(v, curvature, epsilon)
    vs, vq, vt = -C / 15 * r / v, r / v, C * r / v
    return ((d * s, v), (d + s * dv * vs, vs),
            (s * dv * vq, vq), (s * dv * vt, vt), d * r / v)


def curved_map(s, q, t, radius):
    angle = s / radius
    r = .9 + C * (t - s / 15) + q
    v = math.sqrt(r * r - .25)
    return ((radius - v) * math.cos(angle), (radius - v) * math.sin(angle))


def determinant(a, b):
    return a[0] * b[1] - a[1] * b[0]


def integrate(function, low, high, count):
    step = (high - low) / count
    return math.fsum(function(low + (i + .5) * step) for i in range(count)) * step


def write_csv(root, name, rows):
    with (root / name).open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def audits(root):
    inverse_error = jacobian_error = phase_error = derivative_error = identity_error = 0.0
    bounds = []
    derivative_levels = {1e-4: 0.0, 1e-5: 0.0, 1e-6: 0.0}
    samples = 0
    step = 1e-5
    for epsilon in (.1, .25, .5):
        for curvature in (-1/12, 0, 1/50, 1/12):
            for v in (-40, -12, -1e-5, 0, 1e-5, 12, 40):
                d, dv = dilation(v, curvature, epsilon)
                require(d >= 1 and abs(dv) <= 9/8 * abs(curvature) + 1e-14, 'dilation bound')
                for s in (-7.5, -3, 0, 3, 7.5):
                    u = d * s
                    inverse_error = max(inverse_error, abs(u / d - s))
                    # Independent finite differences of the forward Cartesian map.
                    fx = ((d * (s + step) - d * (s - step)) / (2 * step), 0)
                    high = dilation(v + step, curvature, epsilon)[0]
                    low = dilation(v - step, curvature, epsilon)[0]
                    fy = ((high * s - low * s) / (2 * step), 1)
                    jacobian_error = max(jacobian_error, abs(determinant(fx, fy) - d))
                    for t in (0, .05, .3, .65, 1, 4, 12, 15, 16):
                        value = transported(u, v, t, curvature, epsilon)
                        require(all(math.isfinite(x) for x in value), 'nonfinite synthetic value')
                        if curvature * v >= 0:
                            identity_error = max(identity_error, max(abs(a - b) for a, b in
                                zip(value, reference(u, v, t))))
                        for dx in derivative_levels:
                            for axis, derivative in ((0, value[1]), (1, value[2]), (2, value[3])):
                                point = [u, v, t]
                                point[axis] += dx
                                a = transported(*point, curvature, epsilon)[0]
                                point[axis] -= 2 * dx
                                b = transported(*point, curvature, epsilon)[0]
                                derivative_levels[dx] = max(derivative_levels[dx], abs((a - b) / (2 * dx) - derivative))
                        samples += 1
                bounds.append(dict(epsilon=epsilon, curvature=curvature, v=v, dilation=d,
                                   dilation_derivative=dv, determinant=d))
            for v in (-20, -4, -.75, .75, 4, 20):
                for s in (-3, 0, 3):
                    t = 6
                    q = math.sqrt(v * v + .25) - .9 - C * (t - s / 15)
                    position, fs, fq, ft, expected_det = phase_map(s, q, t, curvature, epsilon, -1 if v < 0 else 1)
                    analytic = (fs, fq, ft)
                    for axis in range(3):
                        point = [s, q, t]
                        point[axis] += step
                        high = phase_map(*point, curvature, epsilon, -1 if v < 0 else 1)[0]
                        point[axis] -= 2 * step
                        low = phase_map(*point, curvature, epsilon, -1 if v < 0 else 1)[0]
                        phase_error = max(phase_error, *(abs((high[j] - low[j]) / (2 * step) - analytic[axis][j]) for j in range(2)))
                    phase_error = max(phase_error, abs(determinant(fs, fq) - expected_det))
    require(identity_error == 0, 'inner/zero-curvature identity')
    require(inverse_error < 1e-12 and jacobian_error < 1e-8, 'Cartesian map audit')
    derivative_error = derivative_levels[1e-6]
    require(phase_error < 1e-6 and derivative_error < 2e-5,
            f'chain-rule audit: phase={phase_error}, derivative={derivative_error}')
    write_csv(root, 'maps.csv', bounds)

    # Unequal finite records: no partition normalization, common phase or equal strength assumed.
    collinear_error = cancellation_error = 0.0
    for length, k, born, amplitude in ((2, .2, 0, 1.2), (7.5, 1/15, .5, -.4), (10, .05, 1, .8)):
        parameters = dict(length=length, inverse_speed=k, born=born, amplitude=amplitude)
        for u in (-length-1e-5, -length, -length+1e-5, -length/2, 0, length/2, length-1e-5, length, length+1e-5):
            for v in (-12, 0, 12):
                for t in (born-.01, born, born+.05, born+.3, born+4, born+12, born+15.9, born+16):
                    a = reference(u, v, t, **parameters)
                    b = transported(u, v, t, 0, .25, **parameters)
                    collinear_error = max(collinear_error, *(abs(x-y) for x,y in zip(a,b)))
                    p = transported(u, v, t, -1/12, .25, **parameters)
                    n = transported(u, v, t, -1/12, .25, **(parameters | dict(amplitude=-amplitude)))
                    cancellation_error = max(cancellation_error, *(abs(x+y) for x,y in zip(p,n)))
    require(collinear_error == 0 and cancellation_error == 0, 'finite-source identity/sign audit')

    quadrature = []
    for v in (C * 4 + .7, C * 4 + 1.1):
        d = dilation(v, -1/12, .25)[0]
        for exponent in (0, .5, 1):
            for resolution in (128, 256, 512, 1024):
                for moment in ('signed', 'absolute', 'squared'):
                    def power(h):
                        return abs(h) if moment == 'absolute' else h*h if moment == 'squared' else h
                    a = integrate(lambda s: power(reference(s, v, 4)[0]), -7.5, 7.5, resolution + 17)
                    b = integrate(lambda u: power(transported(u, v, 4, -1/12, .25, exponent)[0]), -d*7.5, d*7.5, resolution + 53)
                    absolute = integrate(lambda s: abs(reference(s, v, 4)[0]), -7.5, 7.5, resolution + 17)
                    expected = d ** (1 - (2 if moment == 'squared' else 1) * exponent)
                    error = abs(b - expected * a) / (absolute if moment != 'squared' else max(a, 1e-12))
                    quadrature.append(dict(v=v, dilation=d, exponent=exponent, moment=moment,
                        reference_cells=resolution+17, world_cells=resolution+53,
                        reference_integral=a, world_integral=b, expected_multiplier=expected,
                        normalized_residual=error))
                    if resolution == 1024:
                        require(error < 1e-5, 'independent-grid quadrature audit')
    write_csv(root, 'transport.csv', quadrature)

    folds = []
    for radius in (12, 50):
        for factor in (.5, 1, 1.5):
            v = radius * factor
            r = math.sqrt(v*v + .25)
            t = (r - .9) / C
            xs = (0, 1-factor)
            # At s=0, N=(-1,0), T=(0,1).
            xs = (C / 15 * r / v, xs[1])
            xq = (-r/v, 0)
            curved_det = determinant(xs, xq)
            high = curved_map(step, 0, t, radius)
            low = curved_map(-step, 0, t, radius)
            fds = tuple((a-b)/(2*step) for a,b in zip(high, low))
            high = curved_map(0, step, t, radius)
            low = curved_map(0, -step, t, radius)
            fdq = tuple((a-b)/(2*step) for a,b in zip(high, low))
            require(abs(determinant(fds, fdq)-curved_det) < 1e-7, 'curved Jacobian audit')
            tangent = math.hypot(*xs)
            if factor == 1:
                require(tangent > .1 and abs(curved_det) < 1e-14, 'nonzero tangent, zero area')
                x0 = curved_map(0, 0, t, radius)
                x1 = curved_map(1, C/15, t, radius)
                require(math.dist(x0, x1) < 1e-12, 'distinct phase preimages at focus')
            folds.append(dict(radius=radius, inner_offset=v, area_ratio=1-factor,
                phase_determinant=curved_det, crest_tangent_norm=tangent,
                relative_crest_stretch=tangent/math.hypot(1,C/15*r/v),
                finite_difference_determinant=determinant(fds,fdq)))
    write_csv(root, 'folds.csv', folds)

    # A synthetic regional counterexample, NOT a reconstructed boat trajectory.
    # In x>7.5 the second baseline record has zero support. The first baseline is bounded
    # by its amplitude1.2, but the second record's outer dilation can raise the sum above1.2.
    point = (7.6, 12.7)
    center_a = (point[0], point[1] - (C*2 + 1.3))
    a = transported(0, point[1]-center_a[1], 5, 1/12, .25, born=3)[0]
    b0 = reference(*point, 5)[0]
    b = transported(*point, 5, -1/12, .25)[0]
    require(b0 == 0 and a+b > 1.2, 'regional inner-side counterexample')

    return dict(scope='Synthetic scalar algebra/map audits only; no production evaluator or gameplay measurements',
        samples=samples, exact_collinear_error=collinear_error, exact_inner_identity_error=identity_error,
        signed_cancellation_error=cancellation_error, inverse_error=inverse_error,
        cartesian_jacobian_error=jacobian_error, phase_chain_error=phase_error,
        scalar_derivative_error=derivative_error, scalar_finite_difference_errors=derivative_levels,
        fine_quadrature_residual=max(x['normalized_residual'] for x in quadrature if x['reference_cells']==1041),
        synthetic_regional_counterexample=dict(point=point, first_inner_contribution=a,
            second_baseline_contribution=b0, second_outer_contribution=b,
            baseline_region_amplitude_bound=1.2, transported_at_point=a+b),
        historical_descriptor='Capture/reset/reuse/predecessor-expiry behavior is a design requirement; no lifecycle implementation is audited',
        unavailable=['production equivalence except the analytic K=0 identity', 'coverage/encounters',
            'CPU/GPU/frame performance', 'production metadata sizes and network/shader integration'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    require(not (args.output/'report.json').exists(), 'use a fresh audit directory')
    result = audits(args.output)
    result['platform'] = platform.platform()
    result['python'] = sys.version
    result['audit_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result['artifacts'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in args.output.glob('*.csv')}
    (args.output/'report.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
