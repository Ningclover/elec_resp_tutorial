#!/usr/bin/env python3
"""Page 5: linearity / gain from a pulser DAC scan.

Reads the per-channel straight-line fits A(Q) = intercept + slope * Q made by
wf_gain_scan_ours.C over the 9 PDVD scan runs (41573 ... 41603, DAC 1 ... 31).
Columns: ch slope(mV/fC) slope_err slope_adc(ADC/fC) intercept tp_ref npoints line_chi2dof

  p5_expected_line.png   what the scan should give: DAC -> fC -> mV -> ADC, and where
                         the 14-bit ADC runs out (why the VD scan stops at DAC 31)
  p5_gain_map.png        fitted gain vs channel, before and after the correction
  p5_gain_dists.png      gain, intercept and straight-line chi2 distributions

Usage:  python3 p5_linearity.py gainscan_vd_ours_daq.txt gainscan_vd_ours_raw.txt [--outdir ../plots]
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from elecresp import coldelec

ADC_PER_MV = 2 ** 14 / 1400.0
FC_PER_DAC = 1.85e-13 * 14.33e-3 * 1e15          # CC (F) * dac step (V) -> fC  = 2.65105
VD_SCAN = [(41573, 1), (41574, 2), (41577, 5), (41582, 10), (41587, 15),
           (41593, 21), (41597, 25), (41602, 30), (41603, 31)]


def plane_of(ch):
    c = ch % 3072
    return 0 if c < 952 else (1 if c < 1904 else 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("daq", help="gain scan BEFORE correction (h_daq)")
    ap.add_argument("raw", help="gain scan AFTER correction (h_raw)")
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(__file__), "..", "plots"))
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)

    # --- expected line ------------------------------------------------------------
    dac = np.arange(0, 41)
    # sampled peak of the ideal 7.8 mV/fC, 2.2 us shape is ~1-2 % under the true peak; use the true peak here
    peak_mv_per_fc = coldelec(np.linspace(0.01, 9, 5000), 7.8, 2.2).max()
    adc = dac * FC_PER_DAC * peak_mv_per_fc * ADC_PER_MV
    fig, ax = plt.subplots(figsize=(6.6, 4.4))
    ax.plot(dac, adc, "k-", label=f"7.8 mV/fC: {FC_PER_DAC * peak_mv_per_fc * ADC_PER_MV:.1f} ADC per DAC step")
    ax.plot([d for _, d in VD_SCAN], [d * FC_PER_DAC * peak_mv_per_fc * ADC_PER_MV for _, d in VD_SCAN],
            "o", color="C3", label="the 9 VD scan runs (DAC 1 ... 31)")
    ax.axhline(16383 - 8200, color="C0", ls="--", lw=1)
    ax.text(1, 16383 - 8200 + 150, "induction (U, V): pedestal ~8200 → room for ~8180 ADC", fontsize=8, color="C0")
    ax.axhline(16383 - 800, color="C2", ls="--", lw=1)
    ax.text(1, 16383 - 800 - 650, "collection (W): pedestal ~800 → room for ~15580 ADC", fontsize=8, color="C2")
    ax.axhline(6300, color="C1", ls=":", lw=1)
    ax.text(1, 6300 + 150, "run 45270 pulses: ~6300 ADC (DAC not recorded; ~26 if 7.8 mV/fC)", fontsize=8, color="C1")
    ax.set_xlabel("pulser DAC setting")
    ax.set_ylabel("expected pulse height [ADC above pedestal]")
    ax.set_title(f"1 DAC = {FC_PER_DAC:.4f} fC  (CC = 185 fF × 14.33 mV)")
    ax.set_ylim(0, 17000)
    ax.legend(fontsize=8, loc="lower right")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p5_expected_line.png"), dpi=130)

    d = np.loadtxt(a.daq)
    r = np.loadtxt(a.raw)
    sel = lambda x: x[(x[:, 1] > 5) & (x[:, 1] < 11)]
    d, r = sel(d), sel(r)

    # --- gain map ---------------------------------------------------------------------
    fig, axs = plt.subplots(2, 1, figsize=(10, 5.5), sharex=True, sharey=True)
    for ax, x, lab in ((axs[0], d, "before correction"), (axs[1], r, "after correction")):
        pl = np.array([plane_of(int(c)) for c in x[:, 0]])
        for p in range(3):
            m = pl == p
            ax.plot(x[m, 0], x[m, 1], ".", ms=1.2, label="UVW"[p])
        ax.axhline(7.8, color="k", lw=0.6, ls="--")
        ax.set_ylabel("gain dA/dQ [mV/fC]")
        ax.set_title(f"{lab}: median {np.median(x[:, 1]):.3f} mV/fC, RMS {np.std(x[:, 1]):.3f}", fontsize=9)
    axs[0].legend(markerscale=8, fontsize=8, ncol=3)
    axs[1].set_xlabel("offline channel")
    axs[0].set_ylim(7.2, 8.8)
    fig.suptitle("Per-channel gain from the 9-run DAC scan (straight-line fit of pulse height vs Q)")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p5_gain_map.png"), dpi=130)

    # --- distributions --------------------------------------------------------------
    fig, axs = plt.subplots(1, 3, figsize=(13, 3.8))
    axs[0].hist(d[:, 1], bins=120, range=(7, 9), histtype="step", color="r",
                label=f"before: {np.median(d[:, 1]):.3f} ± {np.std(d[:, 1]):.3f}")
    axs[0].hist(r[:, 1], bins=120, range=(7, 9), histtype="step", color="b",
                label=f"after: {np.median(r[:, 1]):.3f} ± {np.std(r[:, 1]):.3f}")
    axs[0].set_xlabel("gain [mV/fC]")
    axs[1].hist(d[:, 4], bins=100, range=(-150, 150), histtype="step", color="r")
    axs[1].hist(r[:, 4], bins=100, range=(-150, 150), histtype="step", color="b")
    axs[1].set_xlabel("intercept at Q = 0 [ADC]")
    axs[1].set_title(f"median {np.median(d[:, 4]):.0f} (before), {np.median(r[:, 4]):.0f} (after) ADC",
                     fontsize=9)
    axs[2].hist(np.log10(d[:, 7]), bins=100, histtype="step", color="r")
    axs[2].hist(np.log10(r[:, 7]), bins=100, histtype="step", color="b")
    axs[2].set_xlabel("log10 straight-line χ²/dof")
    axs[0].legend(fontsize=8)
    for ax in axs:
        ax.set_ylabel("channels")
    fig.suptitle(f"9-run VD DAC scan: {len(d)} channels before, {len(r)} after the correction")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p5_gain_dists.png"), dpi=130)
    print(f"ADC per DAC (7.8 mV/fC): {FC_PER_DAC * peak_mv_per_fc * ADC_PER_MV:.1f};  "
          f"1 fC -> {peak_mv_per_fc * ADC_PER_MV:.1f} ADC")
    print(f"gain before {np.median(d[:, 1]):.3f}+-{np.std(d[:, 1]):.3f}, after {np.median(r[:, 1]):.3f}+-{np.std(r[:, 1]):.3f}")
    print(f"intercept median before {np.median(d[:, 4]):.1f}, after {np.median(r[:, 4]):.1f} ADC")


if __name__ == "__main__":
    main()
