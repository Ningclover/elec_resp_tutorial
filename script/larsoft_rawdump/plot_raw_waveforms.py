#!/usr/bin/env python3
"""Plot raw PDVD waveforms from the flat tree written by rawdigit_to_tree.C.

Runs on the host (no LArSoft): needs python3 with numpy, matplotlib, uproot.

    python3 plot_raw_waveforms.py rawdigits_run45270.root [--event N] [--channels U V W] [--outdir DIR]

Makes four PNGs (prefix rawdump_):
    event_display   ADC - baseline, channel vs tick, one panel per plane
    waveforms       full readout of one U, V, W channel
    pulse_zoom      the first positive pulse of the same channels, plus a +-2 % zoom showing the tail
    baseline_noise  baseline and noise (robust RMS) of every channel
The plane of each channel comes from the official channel map (PD2VDTPCChannelMap_v2.txt, on /cvmfs).
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import uproot

TICK_US = 0.512   # native ColdADC sampling; this job has no resampler
PLANES = "UVW"
DEFAULT_MAP = ("/cvmfs/dune.opensciencegrid.org/products/dune/duneprototypes/"
               "v10_21_02d00/config_data/PD2VDTPCChannelMap_v2.txt")


def load_planes(path):
    """offline channel -> plane (0=U, 1=V, 2=W); column 0 = offline channel, column 7 = plane."""
    m = np.loadtxt(path, dtype=np.int64, usecols=(0, 7))
    plane = np.full(m[:, 0].max() + 1, -1, dtype=np.int8)
    plane[m[:, 0]] = m[:, 1]
    return plane


def read_event(path, event):
    t = uproot.open(path)["raw"]
    a = t.arrays(["run", "event", "channel", "adc"], library="np")
    events = np.unique(a["event"])
    ev = events[0] if event is None else event
    sel = a["event"] == ev
    if not sel.any():
        raise SystemExit(f"event {event} not in file; events: {events.tolist()}")
    chans = a["channel"][sel]
    adc = np.stack(a["adc"][sel]).astype(np.float32)
    order = np.argsort(chans)
    return int(a["run"][sel][0]), int(ev), chans[order], adc[order]


def first_positive_pulse(w, before=15, after=100):
    """Cut the first pulse above half of the channel maximum (None if there is none)."""
    thr = 0.5 * w.max()
    if thr < 50:
        return None
    above = np.flatnonzero(w > thr)
    for start in above:
        p = start + np.argmax(w[start:start + 10])
        if p - before >= 0 and p + after < len(w):
            return w[p - before:p + after]
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tree", help="output of rawdigit_to_tree.C")
    ap.add_argument("--event", type=int, default=None, help="event number (default: first in file)")
    ap.add_argument("--channels", type=int, nargs=3, default=[100, 1200, 2000], help="one U, V, W channel")
    ap.add_argument("--chanmap", default=os.environ.get("PDVD_CHANMAP", DEFAULT_MAP))
    ap.add_argument("--outdir", default=".")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)

    run, ev, chans, adc = read_event(a.tree, a.event)
    plane_of = load_planes(a.chanmap)
    plane = plane_of[chans]
    base = np.median(adc, axis=1)                       # baseline = median of the whole readout
    sub = adc - base[:, None]
    nt = adc.shape[1]
    q16, q84 = np.percentile(sub, [16, 84], axis=1)
    noise = 0.5 * (q84 - q16)                           # robust RMS: not pulled up by the pulser pulses
    print(f"run {run} event {ev}: {len(chans)} channels {chans.min()}..{chans.max()}, {nt} ticks")
    for p in range(3):
        m = plane == p
        print(f"  {PLANES[p]}: {m.sum()} channels, median baseline {np.median(base[m]):.0f} ADC, "
              f"median max(ADC - baseline) {np.median(sub[m].max(axis=1)):.0f}")
    tag = f"run {run}, event {ev}"
    row = {c: i for i, c in enumerate(chans)}

    # 1. event display, one panel per plane
    fig, axs = plt.subplots(1, 3, figsize=(14, 4.8))
    for p, ax in enumerate(axs):
        m = plane == p
        im = ax.imshow(sub[m].T, aspect="auto", origin="lower", cmap="RdBu_r", vmin=-3000, vmax=3000,
                       interpolation="nearest", extent=(0, m.sum(), 0, nt))
        ax.set_title(f"{PLANES[p]} plane ({m.sum()} channels)")
        ax.set_xlabel(f"{PLANES[p]} channel index (offline channel order)")
    axs[0].set_ylabel("tick (512 ns)")
    fig.colorbar(im, ax=axs, label="ADC − baseline", fraction=0.025)
    fig.suptitle(f"{tag}: raw waveforms decoded by LArSoft (tpcrawdecoder:daq)")
    fig.savefig(os.path.join(a.outdir, "rawdump_event_display.png"), dpi=120)

    # 2. full readout of three channels
    fig, axs = plt.subplots(3, 1, figsize=(10, 7), sharex=True)
    for ax, ch in zip(axs, a.channels):
        i = row[ch]
        ax.plot(np.arange(nt) * TICK_US / 1000, adc[i], lw=0.6)
        ax.axhline(base[i], color="r", lw=0.6, ls="--", label=f"baseline {base[i]:.0f}")
        ax.set_ylabel("ADC")
        ax.set_title(f"channel {ch} ({PLANES[plane[i]]} plane)", fontsize=9)
        ax.legend(fontsize=8, loc="upper right")
    axs[-1].set_xlabel("time in readout window [ms]")
    fig.suptitle(f"{tag}: raw ADC, {nt} ticks × 512 ns")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "rawdump_waveforms.png"), dpi=120)

    # 3. one pulse and its tail
    fig, axs = plt.subplots(1, 2, figsize=(11, 4))
    for k, ch in enumerate(a.channels):
        i = row[ch]
        p = first_positive_pulse(sub[i])
        if p is None:
            print(f"  channel {ch}: no positive pulse found")
            continue
        x = (np.arange(len(p)) - 15) * TICK_US
        for ax in axs:
            ax.plot(x, p, "-o", ms=2, lw=0.8, color=f"C{k}",
                    label=f"ch {ch} ({PLANES[plane[i]]}), peak {p.max():.0f} ADC")
    axs[0].set_xlim(-8, 20)
    axs[0].set_title("first positive pulse")
    axs[1].set_ylim(-130, 130)
    axs[1].set_title("same pulses, y zoom ±2 % of the peak: the tail")
    for ax in axs:
        ax.axhline(0, color="k", lw=0.5)
        ax.set_xlabel("time from peak [µs]")
        ax.set_ylabel("ADC − baseline")
    axs[0].legend(fontsize=8)
    fig.suptitle(tag)
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "rawdump_pulse_zoom.png"), dpi=120)

    # 4. baseline and noise per channel
    fig, axs = plt.subplots(2, 1, figsize=(10, 6), sharex=True)
    for p in range(3):
        m = plane == p
        axs[0].plot(chans[m], base[m], ".", ms=1.5, label=PLANES[p])
        axs[1].plot(chans[m], noise[m], ".", ms=1.5, label=PLANES[p])
    axs[0].set_ylabel("baseline [ADC]")
    axs[1].set_ylabel("robust RMS [ADC]")
    axs[1].set_ylim(0, np.percentile(noise, 99) * 1.5)
    axs[1].set_xlabel("offline channel")
    axs[0].legend(markerscale=6)
    axs[1].set_title("robust RMS = half the 16–84 % range (includes the pulser pulses' tails)", fontsize=9)
    fig.suptitle(f"{tag}: baseline and noise per channel")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, "rawdump_baseline_noise.png"), dpi=120)
    print(f"wrote rawdump_*.png to {a.outdir}")


if __name__ == "__main__":
    main()
