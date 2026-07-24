#!/usr/bin/env python3
"""
ZK cost model for the emp-zono zonotope network in tests/test_small.cpp.

Derived directly from the source:
  * Input(n)          -> creates n noise symbols (one unique per neuron). K += n.
  * Conv2D / Affine   -> loops k in [0, K); for every output neuron sets
                         new_noise_symbols[k] = inner_product(inputs-with-k).
                         => output becomes STRUCTURALLY DENSE over current K
                         (every later layer sees all K symbols as "present").
                         inner_product with n==0 is free (interval.h L604), n>0
                         costs n interval-multiplies + 2 ZK truncations.
  * ReLU(n)           -> keeps all input symbols (dense) scaled by lambda, adds
                         1 unique symbol per neuron. K += n. Per input symbol a
                         couple of masked interval mults.

So for any Conv/Affine, its input is a ReLU (or Input) output:
    dense part   D = K value BEFORE the feeding ReLU (structural, all neurons)
    sparse part  = feeding ReLU's neuron count, 1 unique symbol per neuron.

Cost terms accumulated over the network:
    MUL   = sum over layers of (#output_neurons * receptive_field * (D + 1))
    TRUNC = sum over layers of (#output_neurons * (D + receptive_field))
            (# structurally-present symbols per output => 2 truncations each)

Total ZK-equivalent work = MUL + ALPHA * TRUNC, calibrated so the current
architecture equals the measured 6.0 h wall-clock.
"""

from dataclasses import dataclass


@dataclass
class Layer:
    kind: str          # 'input', 'conv', 'relu', 'affine', 'output'
    # conv
    in_c: int = 0
    out_c: int = 0
    ih: int = 0
    iw: int = 0
    kh: int = 0
    kw: int = 0
    sh: int = 0
    sw: int = 0
    # affine
    m: int = 0
    n: int = 0
    # input/relu/output
    size: int = 0


def conv_out(hw, k, s, p=0):
    return (hw + 2 * p - k) // s + 1


def analyze(layers, alpha=8.0):
    """Return dict with per-layer and total MUL / TRUNC / neurons."""
    K = 0                     # GLOBAL_NOISE_SYMBOL_CTR
    D_before_feeding = 0      # dense symbol count seen by next conv/affine
    # After a ReLU, the next conv/affine sees dense = K_before_that_relu.
    # We track `dense_for_next` = the D a following Conv/Affine will use.
    dense_for_next = 0
    prev_was_input = False

    total_mul = 0.0
    total_trunc = 0.0
    total_neurons = 0
    rows = []

    for L in layers:
        if L.kind == 'input':
            K += L.size
            dense_for_next = 0            # Conv1 sees sparse input (1 sym/neuron)
            prev_was_input = True
            rows.append((f"Input({L.size})", 0.0, 0.0, 0))

        elif L.kind == 'conv':
            oh = conv_out(L.ih, L.kh, L.sh)
            ow = conv_out(L.iw, L.kw, L.sw)
            out_neurons = L.out_c * oh * ow
            rf = L.in_c * L.kh * L.kw
            if prev_was_input:
                D = 0                      # inputs carry 1 unique sym each
            else:
                D = dense_for_next
            mul = out_neurons * rf * (D + 1)
            trunc = out_neurons * (D + rf)
            total_mul += mul
            total_trunc += trunc
            # output densifies over current K
            dense_for_next = K
            prev_was_input = False
            rows.append((f"Conv {L.in_c}->{L.out_c} "
                         f"k{L.kh}s{L.sh} [{L.out_c}x{oh}x{ow}={out_neurons}]",
                         mul, trunc, 0))

        elif L.kind == 'affine':
            out_neurons = L.n
            rf = L.m
            D = dense_for_next
            mul = out_neurons * rf * (D + 1)
            trunc = out_neurons * (D + rf)
            total_mul += mul
            total_trunc += trunc
            dense_for_next = K
            prev_was_input = False
            rows.append((f"Affine {L.m}->{L.n}", mul, trunc, 0))

        elif L.kind == 'relu':
            # ReLU processes the dense part of its input.
            D = dense_for_next
            mul = 4.0 * L.size * D          # ~4 masked interval mults / symbol
            trunc = 0.0                     # truncation folded into conv/affine
            total_mul += mul
            total_trunc += trunc
            total_neurons += L.size
            # feeding-dense for the *next* conv stays D (shared part), while K grows
            K += L.size
            rows.append((f"ReLU({L.size})", mul, trunc, L.size))

        elif L.kind == 'output':
            rows.append((f"Output({L.size})", 0.0, 0.0, 0))

    cost = total_mul + alpha * total_trunc
    return {
        'rows': rows,
        'mul': total_mul,
        'trunc': total_trunc,
        'cost': cost,
        'neurons': total_neurons,
    }


def C(in_c, out_c, ih, iw, k, s):
    return Layer('conv', in_c=in_c, out_c=out_c, ih=ih, iw=iw, kh=k, kw=k, sh=s, sw=s)


def R(n):
    return Layer('relu', size=n)


def A(m, n):
    return Layer('affine', m=m, n=n)


def report(name, layers, baseline_cost=None, baseline_hours=6.0, alpha=8.0):
    r = analyze(layers, alpha=alpha)
    print(f"\n=== {name} ===")
    for label, mul, trunc, neur in r['rows']:
        if mul or trunc:
            print(f"  {label:42s} mul={mul/1e6:8.1f}M  trunc={trunc/1e6:7.2f}M")
        else:
            print(f"  {label}")
    print(f"  ---- neurons={r['neurons']}  MUL={r['mul']/1e6:.0f}M  "
          f"TRUNC={r['trunc']/1e6:.1f}M  COST={r['cost']/1e6:.0f}M")
    if baseline_cost:
        hrs = baseline_hours * r['cost'] / baseline_cost
        print(f"  ---- predicted wall-clock: {hrs:.2f} h "
              f"({100*r['cost']/baseline_cost:.0f}% of baseline)")
    return r


# --------------------------------------------------------------------------
# Current (REBALANCED) architecture in test_small.cpp  ==  measured 6.0 h
# --------------------------------------------------------------------------
current = [
    Layer('input', size=3072),
    C(3, 16, 32, 32, 4, 2), R(3600),
    C(16, 16, 15, 15, 3, 2), R(784),
    C(16, 24, 7, 7, 3, 2), R(216),
    A(216, 100), R(100),
    A(100, 10), R(10),
    A(10, 10),
    Layer('output', size=10),
]

ALPHA = 8.0
base = analyze(current, alpha=ALPHA)
report("CURRENT (measured 6.0 h)", current, baseline_cost=base['cost'], alpha=ALPHA)


# --------------------------------------------------------------------------
# Search: 3-conv CIFAR stacks, keep neuron budget close to 4710, minimize cost.
# --------------------------------------------------------------------------
def build(c1, c2, c3, aff1_n, aff2_n):
    """Each cX = (out_c, k, s). Returns (layers, ok) chaining spatial dims."""
    layers = [Layer('input', size=3072)]
    ih = iw = 32
    inc = 3
    for c in (c1, c2, c3):
        if c is None:
            continue
        oc, k, s = c
        oh = conv_out(ih, k, s)
        ow = conv_out(iw, k, s)
        if oh < 1 or ow < 1:
            return None
        n = oc * oh * ow
        layers.append(C(inc, oc, ih, iw, k, s))
        layers.append(R(n))
        ih, iw, inc = oh, ow, oc
    flat = inc * ih * iw
    layers.append(A(flat, aff1_n)); layers.append(R(aff1_n))
    layers.append(A(aff1_n, aff2_n)); layers.append(R(aff2_n))
    layers.append(A(aff2_n, 10))
    layers.append(Layer('output', size=10))
    return layers


def search(baseline_cost, neuron_lo=4400, neuron_hi=5000,
           target_hours=4.4, alpha=ALPHA):
    target_cost = baseline_cost * target_hours / 6.0
    cand = []
    oc1s = [12, 16, 20, 24, 32]
    oc2s = [16, 20, 24, 32]
    oc3s = [24, 32, 40, 48, 64]
    ks = [3, 4, 5]
    ss = [2, 3]
    for o1 in oc1s:
        for k1 in ks:
            for s1 in ss:
                for o2 in oc2s:
                    for k2 in [3, 4]:
                        for s2 in [2]:
                            for o3 in oc3s:
                                for k3 in [3]:
                                    for s3 in [1, 2]:
                                        for a1 in [80, 100, 120]:
                                            layers = build((o1, k1, s1),
                                                           (o2, k2, s2),
                                                           (o3, k3, s3),
                                                           a1, 10)
                                            if layers is None:
                                                continue
                                            r = analyze(layers, alpha=alpha)
                                            if not (neuron_lo <= r['neurons'] <= neuron_hi):
                                                continue
                                            cand.append((r['cost'], r['neurons'],
                                                         (o1, k1, s1), (o2, k2, s2),
                                                         (o3, k3, s3), a1, layers))
    cand.sort(key=lambda x: x[0])
    print(f"\n### SEARCH (target <= {target_cost/1e6:.0f}M ~ {target_hours}h, "
          f"neurons in [{neuron_lo},{neuron_hi}]) ###")
    print(f"{'cost(M)':>8} {'hours':>6} {'neur':>5}  conv1        conv2        conv3        aff1")
    for c in cand[:15]:
        cost, neur, cc1, cc2, cc3, a1, _ = c
        hrs = 6.0 * cost / baseline_cost
        print(f"{cost/1e6:8.0f} {hrs:6.2f} {neur:5d}  "
              f"{str(cc1):12s} {str(cc2):12s} {str(cc3):12s} {a1}")
    return cand


cands = search(base['cost'])

if cands:
    best = cands[0]
    report("BEST CANDIDATE", best[-1], baseline_cost=base['cost'], alpha=ALPHA)

# --------------------------------------------------------------------------
# Hand-picked "minimal change from current" variants for comparison.
# --------------------------------------------------------------------------
# Keep Conv1 exactly as current (16ch k4s2 -> 3600), collapse spatial one step
# faster so the Affine flat-width drops 216 -> 96 and Conv3 outputs drop.
minimal = [
    Layer('input', size=3072),
    C(3, 16, 32, 32, 4, 2), R(3600),        # 16x15x15 = 3600  (unchanged)
    C(16, 16, 15, 15, 4, 2), R(576),        # 16x6x6   = 576   (k3->k4, 784->576)
    C(16, 24, 6, 6, 3, 2), R(96),           # 24x2x2   = 96    (216 -> 96)
    A(96, 100), R(100),                     # affine flat 216 -> 96
    A(100, 10), R(10),
    A(10, 10),
    Layer('output', size=10),
]
report("MINIMAL-CHANGE (Conv1 kept, faster collapse)", minimal,
       baseline_cost=base['cost'], alpha=ALPHA)

# Balanced pick from the search: keeps Conv2=16ch, Conv3=24ch, Affine width 100,
# only Conv1 widened 16->20 / k4->k5. Reduces affine flat-width 216 -> 96.
balanced = build((20, 5, 2), (16, 3, 2), (24, 3, 2), 100, 10)
report("BALANCED (search pick, affine width 100)", balanced,
       baseline_cost=base['cost'], alpha=ALPHA)

# Truly minimal: keep Conv1 AND Conv2 exactly as current; only bump Conv3 stride
# 2->3 (3x3 -> 2x2, 216 -> 96 outputs) and shrink the affine flat-width 216 -> 96.
minimal2 = [
    Layer('input', size=3072),
    C(3, 16, 32, 32, 4, 2), R(3600),        # unchanged
    C(16, 16, 15, 15, 3, 2), R(784),        # unchanged
    C(16, 24, 7, 7, 3, 3), R(96),           # s2 -> s3: 24x2x2 = 96 (was 216)
    A(96, 100), R(100),                     # affine flat 216 -> 96
    A(100, 10), R(10),
    A(10, 10),
    Layer('output', size=10),
]
report("MINIMAL2 (Conv1+Conv2 kept, Conv3 s3, affine 96)", minimal2,
       baseline_cost=base['cost'], alpha=ALPHA)

# --------------------------------------------------------------------------
# The architecture CURRENTLY in tests/test_small.cpp (FASTER, 4702 neurons).
# --------------------------------------------------------------------------
faster = [
    Layer('input', size=3072),
    C(3, 20, 32, 32, 5, 2), R(3920),        # 20x14x14 = 3920
    C(20, 16, 14, 14, 3, 2), R(576),        # 16x6x6   = 576
    C(16, 24, 6, 6, 3, 2), R(96),           # 24x2x2   = 96
    A(96, 100), R(100),
    A(100, 10), R(10),
    A(10, 10),
    Layer('output', size=10),
]
report("FASTER (current in test_small.cpp, 4702 neurons)", faster,
       baseline_cost=base['cost'], alpha=ALPHA)

# Naive "add all 160 to the affine hidden" -> a1 = 260.  Hits 4862 exactly but
# the affine layer is the most expensive place to add neurons (rf*D per neuron).
faster_aff260 = build((20, 5, 2), (16, 3, 2), (24, 3, 2), 260, 10)
report("NAIVE 4862 (affine hidden 100 -> 260)", faster_aff260,
       baseline_cost=base['cost'], alpha=ALPHA)

# --------------------------------------------------------------------------
# EXACT-4862 search: force total neurons == 4862 by setting the affine hidden
# width a1 = 4862 - aff2 - conv_relus, so any surplus lives in the CHEAP first
# ReLU (fed by Conv1) instead of the expensive affine.  Minimize ZK cost.
# --------------------------------------------------------------------------
TARGET_NEURONS = 4862
FIVE_H_COST = base['cost'] * 5.0 / 6.0


def conv_relus_and_flat(c1, c2, c3):
    ih = iw = 32
    inc = 3
    tot = 0
    for c in (c1, c2, c3):
        if c is None:
            continue
        oc, k, s = c
        oh = conv_out(ih, k, s)
        ow = conv_out(iw, k, s)
        if oh < 1 or ow < 1:
            return None
        tot += oc * oh * ow
        ih, iw, inc = oh, ow, oc
    return tot, inc * ih * iw


def search_exact(baseline_cost, target=TARGET_NEURONS, aff2=10,
                 a1_lo=40, a1_hi=320, alpha=ALPHA):
    cand = []
    oc1s = [16, 20, 24, 28, 32]
    oc2s = [12, 16, 20, 24, 32]
    oc3s = [16, 24, 32, 40, 48]
    for o1 in oc1s:
        for k1 in [3, 4, 5]:
            for s1 in [2, 3]:
                for o2 in oc2s:
                    for k2 in [3, 4]:
                        for o3 in oc3s:
                            for k3 in [3]:
                                for s3 in [1, 2]:
                                    cf = conv_relus_and_flat(
                                        (o1, k1, s1), (o2, k2, 2), (o3, k3, s3))
                                    if cf is None:
                                        continue
                                    conv_relus, _ = cf
                                    a1 = target - aff2 - conv_relus
                                    if not (a1_lo <= a1 <= a1_hi):
                                        continue
                                    layers = build((o1, k1, s1), (o2, k2, 2),
                                                   (o3, k3, s3), a1, aff2)
                                    if layers is None:
                                        continue
                                    r = analyze(layers, alpha=alpha)
                                    if r['neurons'] != target:
                                        continue
                                    cand.append((r['cost'], a1,
                                                 (o1, k1, s1), (o2, k2, 2),
                                                 (o3, k3, s3), layers))
    cand.sort(key=lambda x: x[0])
    print(f"\n### EXACT-{target} SEARCH  (5h budget = {FIVE_H_COST/1e6:.0f}M) ###")
    print(f"{'cost(M)':>8} {'hours':>6} {'a1':>4}  "
          f"conv1        conv2        conv3")
    for c in cand[:20]:
        cost, a1, cc1, cc2, cc3, _ = c
        hrs = 6.0 * cost / baseline_cost
        flag = '' if cost <= FIVE_H_COST else '   <-- OVER 5h'
        print(f"{cost/1e6:8.0f} {hrs:6.2f} {a1:4d}  "
              f"{str(cc1):12s} {str(cc2):12s} {str(cc3):12s}{flag}")
    return cand


ex = search_exact(base['cost'])
if ex:
    report("BEST EXACT-4862 (under 5h)", ex[0][-1],
           baseline_cost=base['cost'], alpha=ALPHA)


# ==========================================================================
# RECALIBRATION.  The EXACT-4862 config below (663M model-cost, predicted
# 4.74 h under the pure-proportional model) actually MEASURED > 5 h.  So the
# affine-heavy path is under-modeled.  Fit an affine (overhead+slope) model to
# the two real measurements to get a more honest predictor:
#     REBALANCED  840M -> 6.0 h   (measured)
#     EXACT-4862  663M -> ~5.2 h  (measured "just over 5 h")
#   6.0 = a + b*840 ;  5.2 = a + b*663  =>  b, a below.
# This yields a ~2.2 h fixed overhead + slope, so the real 5 h ceiling is at
# ~619M model-cost (not 700M).  Target <= ~570M to keep a safety margin.
# --------------------------------------------------------------------------
CAL = [(840.0, 6.0), (663.0, 5.1)]          # (model-cost Munits, measured h)
                                            # 2nd point = user-confirmed 5.0-5.2h run
_b = (CAL[0][1] - CAL[1][1]) / (CAL[0][0] - CAL[1][0])
_a = CAL[0][1] - _b * CAL[0][0]


def hours_cal(cost_units):
    """Conservative wall-clock estimate from the 2-point overhead+slope fit."""
    return _a + _b * (cost_units / 1e6)


FIVE_H_CAL = (5.0 - _a) / _b * 1e6          # model-cost that maps to 5.0 h
SAFE_COST = 0.92 * FIVE_H_CAL               # aim ~8% under the 5 h line
print(f"\n### RECALIBRATED PREDICTOR  (overhead a={_a:.2f} h, "
      f"slope b={_b*1e3:.3f} h/GMul) ###")
print(f"  5.0 h ceiling  ~= {FIVE_H_CAL/1e6:.0f}M model-cost")
print(f"  safety target  <= {SAFE_COST/1e6:.0f}M  (~{hours_cal(SAFE_COST):.2f} h)")


# ==========================================================================
# AGGRESSIVE EXACT-4862 SEARCH.
# Key lever: Conv2 cost = out2 * rf2 * (D=3072), INDEPENDENT of the first
# ReLU's size (its unique symbols are not yet structurally dense at Conv2).
# rf2 = out_c1 * k2^2.  So we can slash Conv2 by using FEW conv1 channels with
# LARGE spatial (small kernel / stride) -- which keeps the first ReLU big so we
# can still park the 4862 neuron budget there cheaply -- and few conv2 outputs.
# We solve a1 (affine hidden) to hit EXACTLY 4862 and minimise recalibrated h.
# ==========================================================================
def search_aggressive(target=TARGET_NEURONS, aff2=10, a1_lo=16, a1_hi=400,
                      alpha=ALPHA):
    cand = []
    oc1s = [6, 8, 10, 12, 16, 20]
    k1s = [2, 3, 4, 5, 6, 7]
    s1s = [1, 2, 3]
    oc2s = [8, 10, 12, 16, 20, 24]
    k2s = [3, 4]
    s2s = [2, 3]
    oc3s = [8, 12, 16, 20, 24, 32]
    s3s = [1, 2]
    for o1 in oc1s:
        for k1 in k1s:
            for s1 in s1s:
                oh1 = conv_out(32, k1, s1)
                if oh1 < 4:
                    continue
                relu1 = o1 * oh1 * oh1
                if relu1 > target - 40:          # leave room for the rest
                    continue
                for o2 in oc2s:
                    for k2 in k2s:
                        for s2 in s2s:
                            oh2 = conv_out(oh1, k2, s2)
                            if oh2 < 2:
                                continue
                            relu2 = o2 * oh2 * oh2
                            for o3 in oc3s:
                                for s3 in s3s:
                                    oh3 = conv_out(oh2, 3, s3)
                                    if oh3 < 1:
                                        continue
                                    relu3 = o3 * oh3 * oh3
                                    a1 = target - aff2 - relu1 - relu2 - relu3
                                    if not (a1_lo <= a1 <= a1_hi):
                                        continue
                                    layers = build((o1, k1, s1), (o2, k2, s2),
                                                   (o3, 3, s3), a1, aff2)
                                    if layers is None:
                                        continue
                                    r = analyze(layers, alpha=alpha)
                                    if r['neurons'] != target:
                                        continue
                                    cand.append((r['cost'], a1, relu1, relu2,
                                                 relu3, (o1, k1, s1),
                                                 (o2, k2, s2), (o3, 3, s3),
                                                 layers))
    cand.sort(key=lambda x: x[0])
    print(f"\n### AGGRESSIVE EXACT-{target} SEARCH ###")
    print(f"{'cost(M)':>8} {'cal_h':>6} {'r1':>5} {'r2':>4} {'r3':>4} {'a1':>4}"
          f"  conv1        conv2        conv3")
    for c in cand[:20]:
        cost, a1, r1, r2, r3, cc1, cc2, cc3, _ = c
        flag = '' if cost <= FIVE_H_CAL else '  <-- >5h'
        print(f"{cost/1e6:8.0f} {hours_cal(cost):6.2f} {r1:5d} {r2:4d} {r3:4d} "
              f"{a1:4d}  {str(cc1):12s} {str(cc2):12s} {str(cc3):12s}{flag}")
    return cand


agg = search_aggressive()
if agg:
    report("BEST AGGRESSIVE EXACT-4862", agg[0][-1],
           baseline_cost=base['cost'], alpha=ALPHA)
    print(f"  ---- RECALIBRATED wall-clock: {hours_cal(agg[0][0]):.2f} h")


# ==========================================================================
# SENSIBLE EXACT-4862 SEARCH.
# The aggressive search wins by collapsing the conv stack to a handful of
# neurons -- terrible for accuracy, so useless for a same-size comparison.
# Constrain to real conv-net shapes:
#   * conv1 16-24 ch, first ReLU <= ~82% of budget (not absurdly front-loaded)
#   * conv2 >= 16 ch and spatial >= 4x4  (a real second feature map)
#   * conv3 >= 16 ch and spatial >= 2x2
#   * affine hidden 100-320 (moderate; the affine path is under-modeled/risky,
#     so we do NOT balloon it)
# Attack the Conv2 bottleneck the sensible way: shrink its SPATIAL (6x6 -> 4x4
# via k3s3) while keeping/raising channel width.
# ==========================================================================
def search_sensible(target=TARGET_NEURONS, aff2=10, alpha=ALPHA):
    cand = []
    for o1 in [16, 20, 24]:
        for k1 in [4, 5]:
            for s1 in [2]:
                oh1 = conv_out(32, k1, s1)
                relu1 = o1 * oh1 * oh1
                if relu1 > 4100:
                    continue
                for o2 in [16, 20, 24, 32]:
                    for k2 in [3, 4]:
                        for s2 in [2, 3]:
                            oh2 = conv_out(oh1, k2, s2)
                            if oh2 < 4:                 # >= 4x4 feature map
                                continue
                            relu2 = o2 * oh2 * oh2
                            for o3 in [16, 24, 32]:
                                for s3 in [1, 2]:
                                    oh3 = conv_out(oh2, 3, s3)
                                    if oh3 < 2:         # >= 2x2 feature map
                                        continue
                                    relu3 = o3 * oh3 * oh3
                                    a1 = target - aff2 - relu1 - relu2 - relu3
                                    if not (100 <= a1 <= 320):
                                        continue
                                    layers = build((o1, k1, s1), (o2, k2, s2),
                                                   (o3, 3, s3), a1, aff2)
                                    if layers is None:
                                        continue
                                    r = analyze(layers, alpha=alpha)
                                    if r['neurons'] != target:
                                        continue
                                    cand.append((r['cost'], a1, relu1, relu2,
                                                 relu3, (o1, k1, s1),
                                                 (o2, k2, s2), (o3, 3, s3),
                                                 layers))
    cand.sort(key=lambda x: x[0])
    print(f"\n### SENSIBLE EXACT-{target} SEARCH ###")
    print(f"{'cost(M)':>8} {'cal_h':>6} {'r1':>5} {'r2':>4} {'r3':>4} {'a1':>4}"
          f"  conv1        conv2        conv3")
    for c in cand[:20]:
        cost, a1, r1, r2, r3, cc1, cc2, cc3, _ = c
        flag = '' if cost <= FIVE_H_CAL else '  <-- >5h'
        print(f"{cost/1e6:8.0f} {hours_cal(cost):6.2f} {r1:5d} {r2:4d} {r3:4d} "
              f"{a1:4d}  {str(cc1):12s} {str(cc2):12s} {str(cc3):12s}{flag}")
    return cand


sen = search_sensible()
if sen:
    report("BEST SENSIBLE EXACT-4862", sen[0][-1],
           baseline_cost=base['cost'], alpha=ALPHA)
    print(f"  ---- RECALIBRATED wall-clock: {hours_cal(sen[0][0]):.2f} h")


# ==========================================================================
# RELAXED-BUDGET SENSIBLE SEARCH  (user chose: reduce neurons, keep a real
# conv net, get true margin under 5 h).  Same ML-sanity constraints as the
# sensible search, but the neuron budget is a RANGE (4300-4700) instead of a
# hard 4862, so we can drop the ~150-560 most expensive neurons.
# Big lever: Conv2 = out2 * rf2 * 3072.  Shrinking Conv2's SPATIAL 6x6 -> 4x4
# (k3 s3 on the 14x14 map) roughly halves it while keeping real channel width.
# ==========================================================================
def search_relaxed(neuron_lo=4300, neuron_hi=4700, aff2=10, alpha=ALPHA):
    cand = []
    for o1 in [16, 20, 24]:
        for k1 in [4, 5]:
            oh1 = conv_out(32, k1, 2)
            relu1 = o1 * oh1 * oh1
            if not (2800 <= relu1 <= 4000):
                continue
            for o2 in [16, 20, 24, 32]:
                for k2 in [3, 4]:
                    for s2 in [2, 3]:
                        oh2 = conv_out(oh1, k2, s2)
                        if oh2 < 4:                    # >= 4x4 feature map
                            continue
                        relu2 = o2 * oh2 * oh2
                        for o3 in [16, 24, 32]:
                            for s3 in [1, 2]:
                                oh3 = conv_out(oh2, 3, s3)
                                if oh3 < 2:            # >= 2x2 feature map
                                    continue
                                relu3 = o3 * oh3 * oh3
                                for a1 in [100, 128, 160, 192, 224, 256]:
                                    n = relu1 + relu2 + relu3 + a1 + aff2
                                    if not (neuron_lo <= n <= neuron_hi):
                                        continue
                                    layers = build((o1, k1, 2), (o2, k2, s2),
                                                   (o3, 3, s3), a1, aff2)
                                    if layers is None:
                                        continue
                                    r = analyze(layers, alpha=alpha)
                                    cand.append((r['cost'], r['neurons'], a1,
                                                 relu1, relu2, relu3,
                                                 (o1, k1, 2), (o2, k2, s2),
                                                 (o3, 3, s3), layers))
    cand.sort(key=lambda x: x[0])
    print(f"\n### RELAXED SENSIBLE SEARCH  (neurons {neuron_lo}-{neuron_hi}, "
          f"5h line = {FIVE_H_CAL/1e6:.0f}M) ###")
    print(f"{'cost(M)':>8} {'cal_h':>6} {'neur':>5} {'r1':>5} {'r2':>4} "
          f"{'r3':>4} {'a1':>4}  conv1        conv2        conv3")
    for c in cand[:20]:
        cost, neur, a1, r1, r2, r3, cc1, cc2, cc3, _ = c
        flag = '' if cost <= 0.90 * FIVE_H_CAL else '  <-- thin margin'
        print(f"{cost/1e6:8.0f} {hours_cal(cost):6.2f} {neur:5d} {r1:5d} "
              f"{r2:4d} {r3:4d} {a1:4d}  {str(cc1):12s} {str(cc2):12s} "
              f"{str(cc3):12s}{flag}")
    return cand


rel = search_relaxed()
if rel:
    report("BEST RELAXED SENSIBLE", rel[0][-1],
           baseline_cost=base['cost'], alpha=ALPHA)
    print(f"  ---- RECALIBRATED wall-clock: {hours_cal(rel[0][0]):.2f} h")


# ==========================================================================
# NEAR-4802 SEARCH  (user: 4478 is too far below 4802 -- push the count back
# up toward ~4800 WITHOUT losing the sub-5h margin).
#
# Marginal ZK cost per added neuron, by location (mul, at the variant-B D's):
#     first ReLU (Conv1 out) ~  36K   <-- CHEAPEST by 14x
#     affine hidden a1       ~ 502K
#     Conv2 out (relu2)      ~ 581K
#     Conv3 out (relu3)      ~ 1.5M
# So we keep the CHEAP variant-B bottleneck shape fixed -- Conv2 24ch k3s3
# (4x4=384) and Conv3 16ch k3s1 (2x2=64) -- and absorb the +~320 neuron budget
# almost entirely in Conv1's output (widen channels / kernel), keeping the
# affine hidden moderate.  Land the total in [4790, 4862], minimise cal-hours.
# ==========================================================================
def search_near(target_lo=4790, target_hi=4862, aff2=10, alpha=ALPHA):
    cand = []
    # Fixed cheap tail (variant-B): Conv2 24ch k3s3 -> 4x4, Conv3 16ch k3s1 -> 2x2.
    for o1 in range(16, 29):
        for k1 in [4, 5, 6]:
            for s1 in [2]:
                oh1 = conv_out(32, k1, s1)
                if oh1 < 10:
                    continue
                relu1 = o1 * oh1 * oh1
                # Conv2 needs a >=4x4 input map to give 4x4 out at k3s3? k3s3 on
                # oh1 -> conv_out(oh1,3,3); keep it a real >=3x3 map.
                oh2 = conv_out(oh1, 3, 3)
                if oh2 < 3:
                    continue
                relu2 = 24 * oh2 * oh2
                oh3 = conv_out(oh2, 3, 1)
                if oh3 < 2:
                    continue
                relu3 = 16 * oh3 * oh3
                flat = 16 * oh3 * oh3
                for a1 in [32, 40, 50, 60, 72, 84, 92, 100, 110, 120, 128, 140, 150, 160]:
                    n = relu1 + relu2 + relu3 + a1 + aff2
                    if not (target_lo <= n <= target_hi):
                        continue
                    layers = build((o1, k1, s1), (24, 3, 3), (16, 3, 1), a1, aff2)
                    if layers is None:
                        continue
                    r = analyze(layers, alpha=alpha)
                    cand.append((r['cost'], r['neurons'], a1, relu1, relu2,
                                 relu3, (o1, k1, s1), layers))
    cand.sort(key=lambda x: x[0])
    print(f"\n### NEAR-4802 SEARCH  (neurons {target_lo}-{target_hi}, "
          f"5h line = {FIVE_H_CAL/1e6:.0f}M, safe = {0.90*FIVE_H_CAL/1e6:.0f}M) ###")
    print(f"{'cost(M)':>8} {'cal_h':>6} {'neur':>5} {'r1':>5} {'r2':>4} "
          f"{'r3':>4} {'a1':>4}  conv1")
    for c in cand[:24]:
        cost, neur, a1, r1, r2, r3, cc1, _ = c
        flag = '' if cost <= 0.90 * FIVE_H_CAL else '  <-- thin margin'
        print(f"{cost/1e6:8.0f} {hours_cal(cost):6.2f} {neur:5d} {r1:5d} "
              f"{r2:4d} {r3:4d} {a1:4d}  {str(cc1):12s}{flag}")
    return cand


near = search_near(target_lo=4795, target_hi=4865)
if near:
    report("BEST NEAR-4802", near[0][-1],
           baseline_cost=base['cost'], alpha=ALPHA)
    print(f"  ---- RECALIBRATED wall-clock: {hours_cal(near[0][0]):.2f} h")
    # Also surface the config CLOSEST to 4802 among the cheap ones.
    close = sorted(near, key=lambda x: (abs(x[1] - 4802), x[0]))[:8]
    print("\n  --- closest-to-4802 (cheap) options ---")
    for cost, neur, a1, r1, r2, r3, cc1, _ in close:
        print(f"    neur={neur:5d}  cal_h={hours_cal(cost):.2f}  "
              f"cost={cost/1e6:.0f}M  conv1={cc1}  a1={a1}  "
              f"(r1={r1} r2={r2} r3={r3})")

if __name__ == '__main__':
    pass
