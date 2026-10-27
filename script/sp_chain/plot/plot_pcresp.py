#!/usr/bin/env python3
"""Before/after per-channel electronics response in the decon gauss output.

Usage: plot_pcresp.py PLOTDIR [--run RUN] [--release DUNESW_VER]

Reads PLOTDIR/<arm>_<off|on>.root (made by dump_wires.sh) for arm in trad, dnn
and writes into PLOTDIR:
  gauss_<arm>_evt<E>_anode<A>.png   rows U/V/W, cols OFF | ON | ON-OFF
  summary_<arm>.png                 sum|Q| ratio ON/OFF per anode x plane,
                                    plus example waveforms
  summary.txt                       the ratio table + top-anode identity check
"""
import argparse
from pathlib import Path

import numpy as np
import uproot
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm

PLOTS = Path(".")
RELEASE = ""
ARMS = {"trad": "traditional ROI", "dnn": "DNN-ROI + L1SP"}
PLANES = "UVW"
# wclsFrameSaver writes recob::Wire = charge * frame_scale (0.005); undo it so
# the colour scale matches woodpecker plot-frames (FrameFileSink, electrons).
FRAME_SCALE = 0.005


def load(path):
    """-> {event: (channel[], view[], signal[nwire, ntick])}"""
    t = uproot.open(path)["wires"]
    a = t.arrays(["event", "channel", "view", "signal"], library="np")
    out = {}
    for ev in np.unique(a["event"]):
        m = a["event"] == ev
        sig = np.stack(a["signal"][m]).astype(np.float32) / FRAME_SCALE
        out[int(ev)] = (a["channel"][m], a["view"][m], sig)
    return out


def wct_anode(ch):
    """LArSoft channel -> WCT anode ident (protodunevd-wires-larsoft-v5).

    Each CRP holds 3072 channels: U [0,952), V [952,1904), W [1904,3072).
    WCT anode 2k is the first half of every plane of CRP k, 2k+1 the second."""
    crp, loc = ch // 3072, ch % 3072
    start = np.where(loc < 952, 0, np.where(loc < 1904, 952, 1904))
    half = np.where(loc < 1904, 476, 584)
    return 2 * crp + ((loc - start) >= half)


def plane_block(data, anode, plane):
    ch, view, sig = data
    m = (wct_anode(ch) == anode) & (view == plane)
    order = np.argsort(ch[m])
    return ch[m][order], sig[m][order]


def frame_figure(arm, ev, anode, off, on, run):
    # woodpecker plot-frames style: x=channel, y=tick; gauss on hot_r with a
    # fixed 0..1000 scale (white at 0); bipolar ON-OFF on RdBu_r centred at 0
    # with +-10*RMS of the non-zero samples.
    fig, axes = plt.subplots(3, 3, figsize=(21, 16))
    for p in range(3):
        ch, a_off = plane_block(off, anode, p)
        ch2, a_on = plane_block(on, anode, p)
        assert np.array_equal(ch, ch2)
        diff = a_on - a_off
        rms = float(np.std(diff[diff != 0])) if np.any(diff) else 1.0
        ext = [ch[0], ch[-1], 0, a_off.shape[1] - 1]
        for c, (img, title) in enumerate([
                (a_off, "per-channel resp OFF (before)"),
                (a_on, "per-channel resp ON (after)"),
                (diff, "ON − OFF")]):
            ax = axes[p, c]
            if c < 2:
                kw = dict(cmap="hot_r", vmin=0.0, vmax=1000.0)
            else:
                kw = dict(cmap="RdBu_r", norm=TwoSlopeNorm(vcenter=0, vmin=-10 * rms, vmax=10 * rms))
            im = ax.imshow(img.T, aspect="auto", origin="lower", extent=ext,
                           interpolation="none", **kw)
            ax.set_title(f"Plane {PLANES[p]}  (ch {ch[0]}–{ch[-1]},  {len(ch)} wires)  {title}",
                         fontsize=11)
            ax.set_xlabel("Channel")
            ax.set_ylabel("Tick")
            fig.colorbar(im, ax=ax, fraction=0.04, pad=0.01)
        q_off, q_on = np.abs(a_off).sum(), np.abs(a_on).sum()
        axes[p, 2].text(0.02, 0.97, f"Σ|Q| ON/OFF = {q_on / q_off:.3f}" if q_off else "Σ|Q| OFF = 0",
                        transform=axes[p, 2].transAxes, va="top", fontsize=10,
                        bbox=dict(fc="white", ec="0.7", alpha=0.9))
    fig.suptitle(f"PDVD run {run}  event {ev}  anode{anode}  |  tag=gauss  |  {ARMS[arm]} (dunesw {RELEASE})",
                 fontsize=13)
    plt.tight_layout()
    out = PLOTS / f"gauss_{arm}_evt{ev}_anode{anode}.png"
    fig.savefig(out, dpi=110, bbox_inches="tight")
    plt.close(fig)
    return out


def summary(arm, offs, ons, run, log):
    events = sorted(offs)
    # ratio table over all events: sum|Q| ON / sum|Q| OFF
    ratio = np.zeros((4, 3))
    for a in range(4):
        for p in range(3):
            q_off = sum(np.abs(plane_block(offs[e], a, p)[1]).sum() for e in events)
            q_on = sum(np.abs(plane_block(ons[e], a, p)[1]).sum() for e in events)
            ratio[a, p] = q_on / q_off if q_off else np.nan
    log.append(f"\n== {ARMS[arm]}  ({len(events)} events)  sum|Q| ON/OFF")
    log.append("anode      U       V       W")
    for a in range(4):
        log.append(f"  {a}    " + "  ".join(f"{r:.3f}" for r in ratio[a]))
    # top anodes must be untouched
    top_max = 0.0
    n_top = 0
    for e in events:
        ch, _, s_off = offs[e]
        ch2, _, s_on = ons[e]
        assert np.array_equal(ch, ch2)
        m = wct_anode(ch) >= 4
        n_top += int(m.sum())
        if m.any():
            top_max = max(top_max, float(np.abs(s_on[m] - s_off[m]).max()))
    log.append(f"top anodes 4-7: {n_top} wires, max |ON-OFF| = {top_max:g}"
               + ("  (bit-identical)" if n_top and top_max == 0 else ""))

    fig, axes = plt.subplots(1, 4, figsize=(20, 4.6), constrained_layout=True,
                             gridspec_kw=dict(width_ratios=[1.1, 1, 1, 1]))
    ax = axes[0]
    im = ax.imshow(ratio, cmap="RdBu_r", vmin=0.85, vmax=1.15)
    for a in range(4):
        for p in range(3):
            ax.text(p, a, f"{ratio[a, p]:.3f}", ha="center", va="center", fontsize=11)
    ax.set_xticks(range(3), list(PLANES))
    ax.set_yticks(range(4), [f"anode {a}" for a in range(4)])
    ax.set_title(f"Σ|Q| ON / OFF ({len(events)} events)")
    fig.colorbar(im, ax=ax, shrink=0.8)
    # example waveforms: event 0, anode 1, the channel with the most charge per plane
    e0 = events[0]
    for p in range(3):
        ch, a_off = plane_block(offs[e0], 1, p)
        _, a_on = plane_block(ons[e0], 1, p)
        i = int(np.argmax(np.abs(a_off).sum(axis=1)))
        pk = int(np.argmax(a_off[i]))
        lo, hi = max(pk - 150, 0), min(pk + 150, a_off.shape[1])
        t = np.arange(lo, hi)
        ax = axes[p + 1]
        ax.plot(t, a_off[i, lo:hi], color="#1f77b4", lw=1.4, label="OFF (before)")
        ax.plot(t, a_on[i, lo:hi], color="#d62728", lw=1.2, label="ON (after)")
        ax.set_title(f"evt {e0} anode 1 plane {PLANES[p]} ch {ch[i]}")
        ax.set_xlabel("tick")
        ax.set_ylabel("gauss charge [e⁻]")
        ax.legend(frameon=False)
    fig.suptitle(f"PDVD run {run} — {ARMS[arm]}: per-channel electronics response OFF vs ON", fontsize=13)
    out = PLOTS / f"summary_{arm}.png"
    fig.savefig(out, dpi=110)
    plt.close(fig)
    return out


def main():
    global PLOTS, RELEASE
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("plotdir", help="directory holding <arm>_<off|on>.root; PNGs go here")
    ap.add_argument("--run", default="", help="run label for titles, e.g. 039252")
    ap.add_argument("--release", default="v10_26_00d05", help="dunesw version for titles")
    args = ap.parse_args()
    PLOTS, RELEASE, run = Path(args.plotdir), args.release, args.run
    log = [f"PDVD run {run}, dunesw {RELEASE}, decon gauss"]
    for arm in ARMS:
        f_off, f_on = PLOTS / f"{arm}_off.root", PLOTS / f"{arm}_on.root"
        if not (f_off.exists() and f_on.exists()):
            print(f"skip {arm}: missing dump")
            continue
        offs, ons = load(f_off), load(f_on)
        assert sorted(offs) == sorted(ons)
        for ev in sorted(offs):
            for anode in range(4):
                print(frame_figure(arm, ev, anode, offs[ev], ons[ev], run))
        print(summary(arm, offs, ons, run, log))
    (PLOTS / "summary.txt").write_text("\n".join(log) + "\n")
    print("\n".join(log))


if __name__ == "__main__":
    main()
