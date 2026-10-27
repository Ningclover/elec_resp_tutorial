#!/usr/bin/env python3
"""Page 4: fit every channel's average pulser pulse with
   (a) the IDEAL response       A * coldelec(t - t0; tp)                    3 parameters
   (b) the per-channel response A * paramscoldelec(t - t0; tp, k3..k6)      7 parameters

Input is the .npz from make_avg_pulses.py.  Writes fit_<run>.npz (all parameters,
residual RMS) and the plots

  p4_fit_examples.png    data, ideal fit, full fit and residuals for a few channels
  p4_fit_quality.png     residual RMS / peak, ideal vs full fit, all channels
  p4_param_dists.png     fitted tp and k-mismatch, compared with an existing response file
  p4_tp_stability.png    per-channel ideal-fit tp: this run vs an earlier run (stability in time)

Usage:  python3 p4_fit_pulses.py avg_pulses_runNNNNN.npz [--chresp RESPONSE.json.bz2]
            [--tpref gainscan_vd_ours_daq.txt]
            [--channels 100 1200 2000] [--nproc 16] [--outdir ../plots] [--fitout fit.npz]
"""
import argparse
import os
from multiprocessing import Pool

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import least_squares

from elecresp import coldelec, paramscoldelec, load_chresp

PLANES = "UVW"
G = {}


def model_full(p, t):
    t0, A, tp, k3, k4, k5, k6 = p
    return A * paramscoldelec(t - t0, 1.0, tp, k3, k4, k5, k6)


def model_ideal(p, t):
    t0, A, tp = p
    return A * coldelec(t - t0, 1.0, tp)


def canonical(k):
    """(k3,k4) and (k5,k6) can be swapped without changing T(s): put the faster pair (larger k4) first."""
    k = np.array(k, dtype=float)
    swap = k[..., 1] < k[..., 3]
    k[swap] = k[swap][:, [2, 3, 0, 1]] if k.ndim == 2 else k[[2, 3, 0, 1]]
    return k


def fit_one(ch):
    y = G["avg"][ch]
    t = G["t"]
    if not np.all(np.isfinite(y)) or y.max() < 200:
        return ch, np.full(3, np.nan), np.full(7, np.nan), np.nan, np.nan
    np.seterr(all="ignore")
    A0 = y.max() / 1.0
    r_i = least_squares(lambda p: model_ideal(p, t) - y, [-2.6, A0, 2.1],
                        bounds=([-6, 0, 0.8], [1, 10 * A0, 4]))
    t0, A, tp = r_i.x
    best = None
    # a few starting points: the k's are not unique (k3/k4 <-> k5/k6), keep the best
    for k in ([0.2, 0.21, 0.07, 0.075], [0.05, 0.06, 0.5, 0.45], [1.0, 1.1, 0.03, 0.035]):
        r = least_squares(lambda p: model_full(p, t) - y, [t0, A, tp] + k,
                          bounds=([-6, 0, 0.8, -0.5, -0.5, -0.5, -0.5], [1, 10 * A0, 4, 6, 6, 6, 6]))
        if np.isfinite(r.cost) and (best is None or r.cost < best.cost):
            best = r
    rms_i = np.sqrt(np.mean(r_i.fun ** 2)) / y.max()
    rms_f = np.sqrt(np.mean(best.fun ** 2)) / y.max()
    x = best.x.copy()
    x[3:] = canonical(x[3:])
    return ch, r_i.x, x, rms_i, rms_f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("avg")
    ap.add_argument("--chresp", default=None, help="existing response file to compare with")
    ap.add_argument("--tpref", default=None,
                    help="text file with columns ch ... tp_ref (ideal-fit tp of an earlier run), e.g. gainscan_vd_ours_daq.txt")
    ap.add_argument("--channels", type=int, nargs="*", default=[100, 1200, 2000])
    ap.add_argument("--nproc", type=int, default=16)
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(__file__), "..", "plots"))
    ap.add_argument("--fitout", default=None)
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    d = np.load(a.avg)
    run = int(d["run"])
    B, tick = int(d["before"]), float(d["tick_us"])
    G["avg"] = d["avg"]
    G["t"] = (np.arange(d["avg"].shape[1]) - B) * tick
    nch = d["avg"].shape[0]
    plane = d["plane"]

    with Pool(a.nproc) as pool:
        res = pool.map(fit_one, range(nch), chunksize=32)
    pi = np.array([r[1] for r in res])
    pf = np.array([r[2] for r in res])
    rms_i = np.array([r[3] for r in res])
    rms_f = np.array([r[4] for r in res])
    fitout = a.fitout or os.path.join(os.path.dirname(a.avg), f"fit_run{run}.npz")
    np.savez_compressed(fitout, ideal=pi, full=pf, rms_ideal=rms_i, rms_full=rms_f, plane=plane, run=run,
                        names_ideal="t0 A tp", names_full="t0 A tp k3 k4 k5 k6")
    good = np.isfinite(rms_f)
    print(f"fitted {good.sum()} channels -> {fitout}")
    print(f"residual RMS / peak: ideal median {np.nanmedian(rms_i) * 100:.2f} %, "
          f"full median {np.nanmedian(rms_f) * 100:.2f} %")

    # --- examples --------------------------------------------------------------
    t = G["t"]
    tf = np.linspace(t[0], t[-1], 4000)
    fig, axs = plt.subplots(3, len(a.channels), figsize=(4.3 * len(a.channels), 8.5),
                            gridspec_kw=dict(height_ratios=[3, 3, 1.4]), sharex="col")
    for j, ch in enumerate(a.channels):
        y = G["avg"][ch]
        for row, ylim in ((0, None), (1, 0.03)):
            ax = axs[row, j]
            ax.plot(t, y, "ko", ms=2.2, label="data (average pulse)")
            ax.plot(tf, model_ideal(pi[ch], tf), "C0-", lw=1, label=f"ideal fit, tp={pi[ch][2]:.2f} µs")
            ax.plot(tf, model_full(pf[ch], tf), "C3-", lw=1,
                    label="with k3..k6: k=(" + ",".join(f"{v:.3f}" for v in pf[ch][3:]) + ")")
            ax.axhline(0, color="k", lw=0.4)
            if ylim:
                ax.set_ylim(-ylim * y.max(), ylim * y.max())
                ax.set_title("tail zoom (±3 % of peak)", fontsize=9)
            else:
                ax.set_xlim(-8, 40)
                ax.set_title(f"run {run} ch {ch} ({PLANES[plane[ch]]})", fontsize=10)
                ax.legend(fontsize=6.5)
        ax = axs[2, j]
        ax.plot(t, (y - model_ideal(pi[ch], t)) / y.max() * 100, "C0-", lw=0.8,
                label=f"ideal: RMS {rms_i[ch] * 100:.2f} %")
        ax.plot(t, (y - model_full(pf[ch], t)) / y.max() * 100, "C3-", lw=0.8,
                label=f"full: RMS {rms_f[ch] * 100:.2f} %")
        ax.axhline(0, color="k", lw=0.4)
        ax.set_ylim(-4, 4)
        ax.set_xlabel("time from peak sample [µs]")
        ax.legend(fontsize=6.5)
    axs[0, 0].set_ylabel("ADC − pedestal")
    axs[1, 0].set_ylabel("ADC − pedestal")
    axs[2, 0].set_ylabel("residual [% of peak]")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p4_fit_examples.png"), dpi=130)

    # --- fit quality -------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    bins = np.logspace(-3, 0.5, 120)
    ax.hist(rms_i[good] * 100, bins=bins, histtype="step", color="C0",
            label=f"ideal (3 par): median {np.nanmedian(rms_i) * 100:.2f} %")
    ax.hist(rms_f[good] * 100, bins=bins, histtype="step", color="C3",
            label=f"with k3..k6 (7 par): median {np.nanmedian(rms_f) * 100:.2f} %")
    ax.set_xscale("log")
    ax.set_xlabel("residual RMS / pulse height [%]")
    ax.set_ylabel("channels")
    ax.set_title(f"run {run}: how well each shape describes the {good.sum()} channels")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p4_fit_quality.png"), dpi=130)

    # --- parameter distributions + comparison with a response file ------------------
    ref = None
    if a.chresp:
        _, ref = load_chresp(a.chresp)
        kk = canonical(np.stack([ref["k3"], ref["k4"], ref["k5"], ref["k6"]], axis=1))
        ref["k3"], ref["k4"], ref["k5"], ref["k6"] = kk.T
    fig, axs = plt.subplots(1, 3, figsize=(13, 3.8))
    axs[0].hist(pf[good, 2], bins=120, range=(1.8, 2.5), histtype="step", color="C3", density=True,
                label=f"run {run} (this fit)")
    dk34 = pf[good, 3] - pf[good, 4]
    dk56 = pf[good, 5] - pf[good, 6]
    axs[1].hist(np.clip(dk34, -0.3, 0.3), bins=120, range=(-0.3, 0.3), histtype="step", color="C3", density=True)
    axs[2].hist(np.clip(dk56, -0.3, 0.3), bins=120, range=(-0.3, 0.3), histtype="step", color="C3", density=True)
    if ref is not None:
        lab = os.path.basename(a.chresp).replace("protodunevd-params-channel-response-", "").replace(".json.bz2", "")
        axs[0].hist(ref["tp"], bins=120, range=(1.8, 2.5), histtype="step", color="k", density=True,
                    label=f"response file {lab}")
        axs[1].hist(np.clip(ref["k3"] - ref["k4"], -0.3, 0.3), bins=120, range=(-0.3, 0.3), histtype="step",
                    color="k", density=True)
        axs[2].hist(np.clip(ref["k5"] - ref["k6"], -0.3, 0.3), bins=120, range=(-0.3, 0.3), histtype="step",
                    color="k", density=True)
    axs[0].set_xlabel("fitted peaking time tp [µs]")
    axs[1].set_xlabel("k3 − k4 [1/µs]  (clipped)")
    axs[2].set_xlabel("k5 − k6 [1/µs]  (clipped)")
    for ax in axs:
        ax.set_yscale("log")
    axs[0].set_ylabel("channels (normalised)")
    axs[0].legend(fontsize=7)
    fig.suptitle("Fitted per-channel parameters")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p4_param_dists.png"), dpi=130)

    if a.tpref:
        tr = np.loadtxt(a.tpref)
        chs = tr[:, 0].astype(int)
        tpr = tr[:, 5]
        m = (chs < nch) & good[np.minimum(chs, nch - 1)] & (tpr > 1) & (tpr < 4)
        tp_this, tp_ref = pi[chs[m], 2], tpr[m]
        dd = tp_this - tp_ref
        fig, axs = plt.subplots(1, 2, figsize=(11, 4))
        axs[0].plot(tp_ref, tp_this, ",", alpha=0.6)
        axs[0].plot([1.8, 2.4], [1.8, 2.4], "k--", lw=0.8)
        axs[0].set_xlim(1.8, 2.4)
        axs[0].set_ylim(1.8, 2.4)
        axs[0].set_xlabel("ideal-fit tp, earlier run [µs]")
        axs[0].set_ylabel(f"ideal-fit tp, run {run} [µs]")
        axs[0].set_title(f"correlation {np.corrcoef(tp_ref, tp_this)[0, 1]:.2f}", fontsize=9)
        axs[1].hist(np.clip(dd, -0.15, 0.15), bins=120, range=(-0.15, 0.15), histtype="step", color="k")
        hw = (np.percentile(dd, 84) - np.percentile(dd, 16)) / 2
        axs[1].set_xlabel("tp(this run) − tp(earlier run) [µs]")
        axs[1].set_title(f"median {np.median(dd) * 1000:+.1f} ns, 16–84 % half-width {hw * 1000:.1f} ns "
                         f"(spread of tp itself: {np.std(tp_ref) * 1000:.0f} ns)", fontsize=9)
        fig.suptitle(f"Is each channel's shape stable in time?  ({os.path.basename(a.tpref)} vs run {run})")
        fig.tight_layout()
        fig.savefig(os.path.join(a.outdir, "p4_tp_stability.png"), dpi=130)
        print(f"tp(this) - tp(ref): median {np.median(dd) * 1000:+.1f} ns, 16-84% half width {hw * 1000:.1f} ns, "
              f"corr {np.corrcoef(tp_ref, tp_this)[0, 1]:.2f}, ref spread {np.std(tp_ref) * 1000:.0f} ns")
    for pl in range(3):
        m = good & (plane == pl)
        print(f"{PLANES[pl]}: tp median {np.median(pf[m, 2]):.3f} us (ideal-fit tp {np.median(pi[m, 2]):.3f})")


if __name__ == "__main__":
    main()
