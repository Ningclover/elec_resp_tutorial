#!/usr/bin/env python3
"""Cold-electronics (LArASIC) response functions, as used in Wire-Cell.

Two functions, both mirroring wire-cell-toolkit util/src/Response.cxx:

  coldelec(t_us, gain_mVfC, tp_us)
      The IDEAL response (Response::coldelec): 5-pole CR-RC^4-like shaper,
      perfect pole-zero cancellation.  Peak = gain (mV/fC) for 1 fC.

  paramscoldelec(t_us, gain_mVfC, tp_us, k3, k4, k5, k6)
      The per-channel response with IMPERFECT pole-zero cancellation
      (Response::paramscoldelec).  Transfer function

          T(s) = (s+k3)(s+k5) / ((s+k4)(s+k6))
                 * A / ((s+p0) ((s+pr1)^2+pi1^2) ((s+pr2)^2+pi2^2))

      with k3..k6 in 1/us.  k3=k4 and k5=k6 gives back the ideal shape.
      The toolkit uses a long closed-form inverse Laplace transform; here we
      get the same impulse response from the partial-fraction (residue) sum,
      h(t) = sum_i Res_i * exp(p_i t), which is exact and much shorter.
      check_formula_vs_wct.sh compares the two numerically.

Units: time in us, gain in mV/fC, output in mV per fC of injected charge.
WCT stores gain in its internal units; 1 mV/fC = 1.602176634e-13 WCT units
(use gain_wct_to_mVfC()).
"""
import bz2
import json

import numpy as np

MV_PER_FC_WCT = 1.602176634e-13          # value of (wc.mV / wc.fC) in WCT internal units

# Ideal shaper pole positions in units of 1/(tp*CT)   (arXiv:1705.07341, eq. 3.2)
CT = 1.0 / 1.996
P0, PR1, PR2, PI1, PI2, CA = 1.477, 1.417, 1.204, 0.598, 1.299, 2.7433


def gain_wct_to_mVfC(g):
    return np.asarray(g) / MV_PER_FC_WCT


def coldelec(t, gain=7.8, tp=2.2):
    """Ideal response, exact port of Response::coldelec (valid 0 < t < 10 us)."""
    t = np.asarray(t, dtype=float)
    r = t / tp
    g = gain * 10 * 1.012
    e1, e2, e3 = np.exp(-2.94809 * r), np.exp(-2.82833 * r), np.exp(-2.40318 * r)
    c1, s1 = np.cos(1.19361 * r), np.sin(1.19361 * r)
    c2, s2 = np.cos(2.38722 * r), np.sin(2.38722 * r)
    c3, s3 = np.cos(2.5928 * r), np.sin(2.5928 * r)
    c4, s4 = np.cos(5.18561 * r), np.sin(5.18561 * r)
    v = (4.31054 * e1 - 2.6202 * e2 * c1 - 2.6202 * e2 * c1 * c2 + 0.464924 * e3 * c3
         + 0.464924 * e3 * c3 * c4 + 0.762456 * e2 * s1 - 0.762456 * e2 * c2 * s1
         + 0.762456 * e2 * c1 * s2 - 2.6202 * e2 * s1 * s2 - 0.327684 * e3 * s3
         + 0.327684 * e3 * c4 * s3 - 0.327684 * e3 * c3 * s4 + 0.464924 * e3 * s3 * s4) * g
    return np.where((t > 0) & (t < 10), v, 0.0)


def _poles(tp):
    a = 1.0 / (tp * CT)
    return np.array([-P0 * a,
                     -PR1 * a + 1j * PI1 * a, -PR1 * a - 1j * PI1 * a,
                     -PR2 * a + 1j * PI2 * a, -PR2 * a - 1j * PI2 * a]), CA * a ** 4


def paramscoldelec(t, gain, tp, k3, k4, k5, k6, tmax=50.0):
    """Per-channel response, same values as Response::paramscoldelec (valid 0 < t < 50 us)."""
    t = np.asarray(t, dtype=float)
    sp, norm = _poles(tp)
    poles = np.concatenate([sp, [-k4, -k6]]).astype(complex)
    zeros = np.array([-k3, -k5], dtype=complex)
    A = gain * 10.0 * norm
    h = np.zeros_like(t, dtype=complex)
    for i, p in enumerate(poles):
        others = np.delete(poles, i)
        res = A * np.prod(p - zeros) / np.prod(p - others)
        h += res * np.exp(p * t)
    return np.where((t > 0) & (t < tmax), h.real, 0.0)


def transfer(f_MHz, gain, tp, k3=None, k4=None, k5=None, k6=None):
    """|T| on the imaginary axis s = i 2 pi f (f in MHz since time is in us)."""
    s = 2j * np.pi * np.asarray(f_MHz, dtype=float)
    sp, norm = _poles(tp)
    T = gain * 10.0 * norm / np.prod([s - p for p in sp], axis=0)
    if k3 is not None:
        T = T * (s + k3) * (s + k5) / ((s + k4) * (s + k6))
    return T


def load_chresp(path):
    """Read a ParamsPerChannelResponse JSON(.bz2).  Returns header dict and a
    structured array with channel, gain (mV/fC), shaping (us), k3..k6 (1/us)."""
    op = bz2.open if str(path).endswith(".bz2") else open
    with op(path, "rt") as f:
        top = json.load(f)
    rows = []
    for ci in top["channel_info"]:
        for ch in ci["channels"]:
            rows.append((ch, ci["gain"] / MV_PER_FC_WCT, ci["shaping"] / 1000.0,
                         ci["k3"], ci["k4"], ci["k5"], ci["k6"]))
    dt = [("ch", int), ("gain", float), ("tp", float),
          ("k3", float), ("k4", float), ("k5", float), ("k6", float)]
    arr = np.array(sorted(rows), dtype=dt)
    hdr = {k: top[k] for k in ("tick", "t0", "nsamp")}
    return hdr, arr


def sample_like_wct(hdr, func, *args):
    """Sample a response exactly like ParamsPerChannelResponse: bin centres of
    Binning(nsamp, t0, t0+nsamp*tick), header values in ns."""
    tick, t0, n = hdr["tick"] / 1000.0, hdr["t0"] / 1000.0, hdr["nsamp"]
    tc = t0 + (np.arange(n) + 0.5) * tick
    return tc, func(tc, *args)
