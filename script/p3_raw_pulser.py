#!/usr/bin/env python3
"""Page 3 plots: what raw cold-electronics PULSER data look like.

Reads one PDVD bottom-drift raw HDF5 calibration file directly (no LArSoft).

  p3_full_readout.png     one U, V, W channel over the whole readout window:
                          alternating +/- pulses on the pedestal
  p3_event_display.png    all 6144 channels x ticks (pedestal subtracted)
  p3_one_pulse.png        one positive pulse, U / V / W, and a x50 zoom of its tail
  p3_pulse_stack.png      every positive pulse of one channel (all trigger records)
                          on top of each other + their average
  p3_amplitude_map.png    pulse height and pedestal per channel

Usage:  python3 p3_raw_pulser.py FILE.hdf5 [--channels 100 1200 2000] [--outdir ../plots]
"""
import argparse
import os

import h5py
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from pdvd_hdf5_decode import decode_record, list_records, load_channel_map

TICK = 0.512
PLANES = "UVW"


def find_pulses(w, frac=0.5):
    """Indices of positive local maxima above frac * max (pedestal-subtracted waveform)."""
    thr = frac * w.max()
    m = (w[1:-1] > w[:-2]) & (w[1:-1] >= w[2:]) & (w[1:-1] > thr)
    return np.where(m)[0] + 1


def cut_pulses(w, before=15, after=100):
    out = []
    for p in find_pulses(w):
        if p - before >= 0 and p + after < len(w):
            out.append(w[p - before:p + after])
    return np.array(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hdf5")
    ap.add_argument("--channels", type=int, nargs=3, default=[100, 1200, 2000], help="one U, V, W channel")
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(__file__), "..", "plots"))
    ap.add_argument("--nrec", type=int, default=0, help="records for the pulse stack (0 = all)")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    cmap = load_channel_map()
    plane = cmap[1]

    h5 = h5py.File(a.hdf5, "r")
    run = int(h5.attrs["run_number"])
    recs = list_records(h5)
    adc, _ = decode_record(h5, recs[0], cmap)
    ped = np.median(adc, axis=1).astype(float)
    sub = adc - ped[:, None]
    nt = adc.shape[1]
    tus = np.arange(nt) * TICK

    # --- full readout --------------------------------------------------------
    fig, axs = plt.subplots(3, 1, figsize=(10, 7), sharex=True)
    for ax, ch in zip(axs, a.channels):
        ax.plot(tus / 1000, adc[ch], lw=0.6)
        ax.axhline(ped[ch], color="r", lw=0.6, ls="--")
        ax.set_ylabel("ADC")
        ax.set_title(f"channel {ch} ({PLANES[plane[ch]]} plane): pedestal {ped[ch]:.0f} ADC", fontsize=9)
        ax.set_ylim(-300, 16700)
    axs[-1].set_xlabel("time in readout window [ms]")
    fig.suptitle(f"run {run}, {recs[0]}: raw ADC, {nt} ticks x 512 ns")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p3_full_readout.png"), dpi=130)

    # --- event display ---------------------------------------------------------
    fig, ax = plt.subplots(figsize=(10, 5))
    im = ax.imshow(sub.T, aspect="auto", origin="lower", cmap="RdBu_r", vmin=-3000, vmax=3000,
                   interpolation="nearest", extent=(0, sub.shape[0], 0, nt))
    for c in (952, 1904, 3072, 3072 + 952, 3072 + 1904):
        ax.axvline(c, color="k", lw=0.5)
    ax.set_xlabel("offline channel")
    ax.set_ylabel("tick (512 ns)")
    ax.set_title(f"run {run} {recs[0]}: ADC − pedestal (lines: U | V | W per CRP)")
    fig.colorbar(im, ax=ax, label="ADC")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p3_event_display.png"), dpi=130)

    # --- one pulse + tail --------------------------------------------------------
    fig, axs = plt.subplots(1, 2, figsize=(11, 4))
    for i, ch in enumerate(a.channels):
        p = cut_pulses(sub[ch])[0]
        x = (np.arange(len(p)) - 15) * TICK
        for ax in axs:
            ax.plot(x, p, "-o", ms=2, lw=0.8, color=f"C{i}",
                    label=f"ch {ch} ({PLANES[plane[ch]]}), peak {p.max():.0f} ADC")
    axs[0].set_xlim(-8, 20)
    axs[1].set_ylim(-0.02 * 6500, 0.02 * 6500)
    axs[1].set_title("same pulses, y zoom ±2 % of the peak: the TAIL")
    axs[0].set_title("one positive pulse")
    for ax in axs:
        ax.axhline(0, color="k", lw=0.5)
        ax.set_xlabel("time from peak [µs]")
        ax.set_ylabel("ADC − pedestal")
    axs[0].legend(fontsize=8)
    fig.suptitle(f"run {run}: single pulser pulse, 15 ticks before to 100 ticks after the peak")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p3_one_pulse.png"), dpi=130)

    # --- pulse stack over records ------------------------------------------------
    use = recs if a.nrec == 0 else recs[:a.nrec]
    stacks = {ch: [] for ch in a.channels}
    for r in use:
        ad, _ = decode_record(h5, r, cmap)
        for ch in a.channels:
            w = ad[ch].astype(float)
            w -= np.median(w)
            stacks[ch].extend(cut_pulses(w))
    fig, axs = plt.subplots(1, 3, figsize=(13, 3.8))
    for ax, ch in zip(axs, a.channels):
        s = np.array(stacks[ch])
        x = (np.arange(s.shape[1]) - 15) * TICK
        for w in s:
            ax.plot(x, w, color="0.7", lw=0.4)
        ax.plot(x, s.mean(axis=0), color="r", lw=1.2, label=f"average of {len(s)} pulses")
        pk = s.max(axis=1)
        ax.set_title(f"ch {ch} ({PLANES[plane[ch]]}): peak {pk.mean():.0f} ± {pk.std():.0f} ADC", fontsize=9)
        ax.set_xlim(-8, 25)
        ax.set_xlabel("time from peak [µs]")
        ax.legend(fontsize=8)
    axs[0].set_ylabel("ADC − pedestal")
    fig.suptitle(f"run {run}: all positive pulses in {len(use)} trigger records")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p3_pulse_stack.png"), dpi=130)

    # --- per-channel amplitude / pedestal ---------------------------------------
    amp = sub.max(axis=1)
    fig, axs = plt.subplots(2, 1, figsize=(10, 5.5), sharex=True)
    for pl in range(3):
        m = plane[:sub.shape[0]] == pl
        chs = np.arange(sub.shape[0])[m]
        axs[0].plot(chs, ped[m], ".", ms=1.5, label=PLANES[pl])
        axs[1].plot(chs, amp[m], ".", ms=1.5, label=PLANES[pl])
    axs[0].set_ylabel("pedestal [ADC]")
    axs[1].set_ylabel("max − pedestal [ADC]")
    axs[1].set_xlabel("offline channel")
    axs[0].legend(markerscale=6, fontsize=8)
    axs[0].set_title(f"run {run} {recs[0]}: induction (U, V) sits mid-range, collection (W) near the bottom")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "p3_amplitude_map.png"), dpi=130)

    for pl in range(3):
        m = plane[:sub.shape[0]] == pl
        print(f"{PLANES[pl]}: median pedestal {np.median(ped[m]):.0f}, median pulse height {np.median(amp[m]):.0f} ADC")
    print("pulse spacing (ticks):", np.diff(find_pulses(sub[a.channels[0]]))[:5])


if __name__ == "__main__":
    main()
