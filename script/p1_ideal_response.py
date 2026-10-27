#!/usr/bin/env python3
"""Page 1 plots: the IDEAL cold-electronics response.

  p1_ideal_vs_peaking_time.png  shape for the 4 LArASIC peaking-time settings
  p1_ideal_vs_gain.png          same shape, 4 gain settings (height = gain x Q)
  p1_ideal_sampled.png          the PDVD nominal (7.8 mV/fC, 2.2 us) sampled at 512 ns,
                                in mV and in ADC for a 1 fC and a 26.5 fC (10 DAC) pulse
  p1_ideal_spectrum.png         |T(f)| for the 4 peaking times, with the 512 ns Nyquist line

Usage:  python3 p1_ideal_response.py [--outdir ../plots]
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from elecresp import coldelec, transfer

ADC_PER_MV = 2 ** 14 / 1400.0       # 14-bit ColdADC over 0.2-1.6 V  -> 11.703 ADC/mV
FC_PER_DAC = 2.65105                # pulser charge per DAC step at 7.8 mV/fC (see page 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(__file__), "..", "plots"))
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    t = np.linspace(-1, 12, 2601)

    # --- peaking time ----------------------------------------------------------
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for tp in (0.5, 1.0, 2.0, 3.0):
        y = coldelec(t, 14.0, tp)
        ax.plot(t, y, label=f"$t_p$ = {tp} µs (peak at {t[np.argmax(y)]:.2f} µs)")
    ax.set_xlabel("time after charge arrives [µs]")
    ax.set_ylabel("output [mV] for 1 fC")
    ax.set_title("Ideal LArASIC response, gain 14 mV/fC")
    ax.axhline(0, color="k", lw=0.5)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p1_ideal_vs_peaking_time.png"), dpi=130)

    # --- gain ------------------------------------------------------------------
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for g in (4.7, 7.8, 14.0, 25.0):
        ax.plot(t, coldelec(t, g, 2.2), label=f"{g} mV/fC")
    ax.set_xlabel("time [µs]")
    ax.set_ylabel("output [mV] for 1 fC")
    ax.set_title("Ideal response, $t_p$ = 2.2 µs, four gain settings")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p1_ideal_vs_gain.png"), dpi=130)

    # --- sampled nominal PDVD --------------------------------------------------
    tick = 0.512
    ts = np.arange(-2, 24) * tick + 0.3
    fig, axs = plt.subplots(1, 2, figsize=(10, 3.8))
    axs[0].plot(t, coldelec(t, 7.8, 2.2), color="C0", label="continuous")
    axs[0].plot(ts, coldelec(ts, 7.8, 2.2), "o", ms=4, color="C3", label="sampled every 512 ns")
    axs[0].set_xlabel("time [µs]")
    axs[0].set_ylabel("output [mV] for 1 fC")
    axs[0].set_title("PDVD nominal: 7.8 mV/fC, 2.2 µs")
    axs[0].legend(fontsize=8)
    q = 10 * FC_PER_DAC
    adc = coldelec(t, 7.8, 2.2) * q * ADC_PER_MV
    axs[1].plot(t / tick, adc, color="C0")
    axs[1].plot(ts / tick, coldelec(ts, 7.8, 2.2) * q * ADC_PER_MV, "o", ms=4, color="C3")
    axs[1].set_xlabel("time [ticks of 512 ns]")
    axs[1].set_ylabel("ADC above pedestal")
    axs[1].set_title(f"same, for a 10-DAC pulse ({q:.1f} fC): peak {adc.max():.0f} ADC")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p1_ideal_sampled.png"), dpi=130)

    # --- spectrum --------------------------------------------------------------
    f = np.linspace(0.001, 2.0, 2000)          # MHz
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for tp in (0.5, 1.0, 2.0, 3.0):
        T = np.abs(transfer(f, 14.0, tp))
        ax.plot(f, T / T.max(), label=f"$t_p$ = {tp} µs")
    ax.axvline(1 / (2 * 0.512), color="k", ls="--", lw=1)
    ax.text(1 / (2 * 0.512) + 0.02, 0.8, "Nyquist\n(512 ns ticks)", fontsize=8)
    ax.set_xlabel("frequency [MHz]")
    ax.set_ylabel("|T(f)|  (normalised to max)")
    ax.set_title("The shaper is a low-pass filter")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p1_ideal_spectrum.png"), dpi=130)

    y = coldelec(t, 7.8, 2.2)
    print(f"nominal PDVD: peak {y.max():.3f} mV/fC at t = {t[np.argmax(y)]:.2f} us; "
          f"1 fC -> {y.max() * ADC_PER_MV:.1f} ADC; ADC/mV = {ADC_PER_MV:.3f}")


if __name__ == "__main__":
    main()
