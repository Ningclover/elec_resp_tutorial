#!/usr/bin/env python3
"""Page 2 plots: the TAIL from imperfect pole-zero cancellation.

  p2_toy_tails.png        k3 vs k4 (and k5 vs k6) mismatch -> overshoot / undershoot tail
  p2_real_channels.png    real fitted PDVD channels (response file) vs the ideal shape
  p2_ratio_spectrum.png   |E_channel(f)| / |E_ideal(f)| for the same channels: the
                          correction factor SP has to apply is largest at low frequency
  p2_tail_fraction.png    distribution over all channels of the tail area / peak area

Usage:  python3 p2_pole_zero_tail.py RESPONSE.json.bz2 [--outdir ../plots] [--channels 100 1200 2000 3300]
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from elecresp import coldelec, paramscoldelec, transfer, load_chresp

PLANE = {0: "U", 1: "V", 2: "W"}


def plane_of(ch):
    # PDVD bottom CRP numbering, offline channels 0-6143: per 3072-ch CRP, U [0,952) V [952,1904) W [1904,3072)
    c = ch % 3072
    return "U" if c < 952 else ("V" if c < 1904 else "W")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("chresp")
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(__file__), "..", "plots"))
    ap.add_argument("--channels", type=int, nargs="*", default=None)
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    np.seterr(all="ignore")
    hdr, P = load_chresp(a.chresp)
    t = np.linspace(-1, 45, 4601)

    # --- toy tails -------------------------------------------------------------
    fig, axs = plt.subplots(1, 2, figsize=(11, 4))
    ideal = paramscoldelec(t, 7.8, 2.2, 0.2, 0.2, 0.07, 0.07)
    cases = [((0.2, 0.2, 0.07, 0.07), "k3=k4, k5=k6 (ideal)", "k"),
             ((0.24, 0.2, 0.07, 0.07), "k3 > k4 : positive tail", "C3"),
             ((0.16, 0.2, 0.07, 0.07), "k3 < k4 : negative tail", "C0"),
             ((0.2, 0.2, 0.09, 0.07), "k5 > k6 : longer positive tail", "C1"),
             ((0.4, 0.4, 0.07, 0.07), "k3 = k4 = 0.4 (still ideal)", "C2")]
    for k, lab, c in cases:
        y = paramscoldelec(t, 7.8, 2.2, *k)
        ls = ":" if c == "C2" else "-"
        for ax in axs:
            ax.plot(t, y / ideal.max(), color=c, ls=ls, label=lab)
    axs[0].set_xlim(-1, 12)
    axs[0].set_title("Full scale: the peak looks the same")
    axs[1].set_ylim(-0.03, 0.03)
    axs[1].set_title("Zoom ×30 on the tail")
    for ax in axs:
        ax.axhline(0, color="k", lw=0.5)
        ax.set_xlabel("time [µs]")
        ax.set_ylabel("response / ideal peak")
    axs[1].legend(fontsize=8)
    fig.suptitle("Imperfect pole-zero cancellation: (s+k3)(s+k5)/((s+k4)(s+k6)), k in 1/µs  (tp = 2.2 µs)")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p2_toy_tails.png"), dpi=130)

    # --- real channels ---------------------------------------------------------
    good = (P["k4"] != P["k6"])
    if a.channels:
        chans = a.channels
    else:   # most-positive tail, most-negative tail, and a typical channel
        tailfrac = []
        for r in P:
            y = paramscoldelec(t, r["gain"], r["tp"], r["k3"], r["k4"], r["k5"], r["k6"])
            tailfrac.append(np.trapezoid(np.where(t > 3 * r["tp"], y, 0), t) / np.trapezoid(y, t))
        tailfrac = np.array(tailfrac)
        ok = good & np.isfinite(tailfrac)
        idx = np.where(ok)[0]
        chans = [P["ch"][idx[np.argmax(tailfrac[idx])]], P["ch"][idx[np.argmin(tailfrac[idx])]],
                 P["ch"][idx[np.argmin(np.abs(tailfrac[idx] - np.median(tailfrac[idx])))]]]
    fig, axs = plt.subplots(1, 2, figsize=(11, 4))
    fig2, ax2 = plt.subplots(figsize=(6.4, 4.2))
    f = np.linspace(0.0005, 0.98, 3000)
    for i, ch in enumerate(chans):
        r = P[P["ch"] == ch][0]
        y = paramscoldelec(t, r["gain"], r["tp"], r["k3"], r["k4"], r["k5"], r["k6"])
        yi = coldelec(t, r["gain"], r["tp"])
        lab = (f"ch {ch} ({plane_of(ch)}): g={r['gain']:.2f}, tp={r['tp']:.2f}, "
               f"k=({r['k3']:.3f},{r['k4']:.3f},{r['k5']:.3f},{r['k6']:.3f})")
        for ax in axs:
            ax.plot(t, y, color=f"C{i}", label=lab)
            ax.plot(t, yi, color=f"C{i}", ls="--", lw=0.8)
        R = np.abs(transfer(f, r["gain"], r["tp"], r["k3"], r["k4"], r["k5"], r["k6"])) / \
            np.abs(transfer(f, 7.8, 2.2))
        ax2.semilogx(f * 1000, R, color=f"C{i}", label=f"ch {ch}")
    axs[0].set_xlim(-1, 12)
    axs[1].set_ylim(-0.25, 0.25)
    for ax in axs:
        ax.axhline(0, color="k", lw=0.5)
        ax.set_xlabel("time [µs]")
        ax.set_ylabel("mV for 1 fC")
    axs[0].set_title("fitted (solid) vs ideal with same g, tp (dashed)")
    axs[1].set_title("tail zoom")
    axs[0].legend(fontsize=6.5, loc="upper right")
    fig.suptitle(f"Real PDVD channels from {os.path.basename(a.chresp)}")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p2_real_channels.png"), dpi=130)
    ax2.axhline(1, color="k", lw=0.5)
    ax2.set_xlabel("frequency [kHz]")
    ax2.set_ylabel("|E_channel(f)| / |E_ideal(f)|")
    ax2.set_title("What SP must divide out (ideal = 7.8 mV/fC, 2.2 µs)")
    ax2.legend(fontsize=8)
    fig2.tight_layout()
    fig2.savefig(os.path.join(a.outdir, "p2_ratio_spectrum.png"), dpi=130)

    # --- tail fraction over all channels ----------------------------------------
    if not a.channels:
        fig, ax = plt.subplots(figsize=(6.4, 4.2))
        v = tailfrac[ok] * 100
        ax.hist(np.clip(v, -10, 10), bins=200, range=(-10, 10), histtype="step", color="k")
        ax.set_yscale("log")
        ax.set_xlabel("tail area after 3·tp / total area  [%]  (clipped at ±10)")
        ax.set_ylabel("channels")
        ax.set_title(f"{ok.sum()} channels: median {np.median(v):+.2f} %, "
                     f"|tail| > 2 % in {np.mean(np.abs(v) > 2) * 100:.0f} %")
        fig.tight_layout()
        fig.savefig(os.path.join(a.outdir, "p2_tail_fraction.png"), dpi=130)
        print("example channels:", chans, " k4==k6 (NaN) channels:", P["ch"][~good])


if __name__ == "__main__":
    main()
