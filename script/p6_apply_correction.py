#!/usr/bin/env python3
"""Page 6: apply the per-channel correction to real pulser waveforms, the same
way Wire-Cell does it (ResponseShaper.cxx / OmnibusSigProc::decon_2D_init):

    spectrum(f) *= E_ideal(f) / E_channel(f),   then drop DC and re-baseline

E_channel = the fitted per-channel response (p4_fit_pulses.py output), sampled
like ParamsPerChannelResponse (nsamp=100 bins from t0=-2.75 us, zero-padded);
E_ideal   = ColdElecResponse 7.8 mV/fC, 2.2 us sampled on the same grid start.

Gain: E_channel needs an absolute gain in mV/fC = peak_ADC / (Q * 11.703).
The injected charge Q of the example run is not recorded, so by default Q is
chosen so that the MEDIAN channel gain equals 7.8 mV/fC (--charge-fC overrides).

Plots
  p6_correction_spectrum.png  |E_ideal/E_channel| for the example channels
  p6_before_after.png         one raw pulse before/after, full and tail zoom
  p6_before_after_tp.png      ideal-fit peaking time and pulse height/Q, all channels,
                              before vs after (the PDVD version of slide 6 of 20260701.pdf)

With --chresp RESPONSE.json.bz2 the per-channel response is taken from that
response file instead of the fit (gain, shaping, k3..k6 exactly as Wire-Cell reads
them); plots then get the suffix _chresp.  The fit file is still needed for nothing
else than the plane list, so the run's own fit can be compared with an older file.

Usage:  python3 p6_apply_correction.py FILE.hdf5 fit_runNNNNN.npz [--channels 100 1200 2000]
            [--charge-fC Q] [--chresp RESPONSE.json.bz2] [--nproc 32] [--outdir ../plots]
"""
import argparse
import os
from multiprocessing import Pool

import h5py
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import least_squares

from elecresp import coldelec, paramscoldelec, load_chresp
from pdvd_hdf5_decode import decode_record, list_records, load_channel_map

TICK = 0.512
ADC_PER_MV = 2 ** 14 / 1400.0
T0, NSAMP = -2.75, 100           # header of the PDVD response files
G_IDEAL, TP_IDEAL = 7.8, 2.2
PLANES = "UVW"
G = {}


def channel_response(par, gain_mVfC):
    """Per-channel response on the ParamsPerChannelResponse grid (bin centres)."""
    _, _, tp, k3, k4, k5, k6 = par
    tc = T0 + (np.arange(NSAMP) + 0.5) * TICK
    return paramscoldelec(tc, gain_mVfC, tp, k3, k4, k5, k6)


def correct(signal, par, gain_mVfC):
    n = len(signal)
    tc = T0 + (np.arange(n) + 0.5) * TICK
    elec = np.fft.rfft(coldelec(tc, G_IDEAL, TP_IDEAL))
    ch = np.zeros(n)
    ch[:NSAMP] = channel_response(par, gain_mVfC)
    che = np.fft.rfft(ch)
    spec = np.fft.rfft(signal)
    ratio = np.where(np.abs(che) > 0, elec / np.where(che == 0, 1, che), 0)
    spec *= ratio
    spec[0] = 0
    out = np.fft.irfft(spec, n)
    mu, sd = out.mean(), out.std()
    base = np.median(np.where(np.abs(out - mu) > 6 * sd, mu, out))
    return out - base


def avg_pulses(w, B=15, A=100):
    mx = w.max()
    p = np.where((w[1:-1] > w[:-2]) & (w[1:-1] >= w[2:]) & (w[1:-1] > 0.5 * mx))[0] + 1
    p = p[(p - B >= 0) & (p + A < len(w))]
    return np.mean([w[i - B:i + A] for i in p], axis=0) if len(p) else None


def ideal_fit(y):
    t = (np.arange(len(y)) - 15) * TICK
    r = least_squares(lambda p: p[1] * coldelec(t - p[0], 1.0, p[2]) - y, [-2.6, y.max(), 2.1],
                      bounds=([-6, 0, 0.8], [1, 10 * y.max(), 4]))
    return r.x


def work(ch):
    np.seterr(all="ignore")
    par = G["full"][ch]
    w = G["sub"][ch]
    if not np.all(np.isfinite(par[2:])) or not np.isfinite(G["gain"][ch]) or w.max() < 200:
        return ch, np.nan, np.nan, np.nan, np.nan
    yb = avg_pulses(w)
    ya = avg_pulses(correct(w, par, G["gain"][ch]))
    if yb is None or ya is None:
        return ch, np.nan, np.nan, np.nan, np.nan
    fb, fa = ideal_fit(yb), ideal_fit(ya)
    return ch, fb[2], fa[2], yb.max(), ya.max()


def shape_peak_of(full):
    tf = np.linspace(-1, 15, 1601)
    return np.array([np.nanmax(paramscoldelec(tf, 1.0, *p[2:])) if np.all(np.isfinite(p[2:])) else np.nan
                     for p in full])


def hw(x):
    """robust width: half of the 16-84 % interval"""
    return (np.percentile(x, 84) - np.percentile(x, 16)) / 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hdf5")
    ap.add_argument("fit")
    ap.add_argument("--channels", type=int, nargs=3, default=[100, 1200, 2000])
    ap.add_argument("--charge-fC", type=float, default=None)
    ap.add_argument("--record", type=int, default=0)
    ap.add_argument("--chresp", default=None, help="use this response file instead of the fit")
    ap.add_argument("--nproc", type=int, default=32)
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(__file__), "..", "plots"))
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    np.seterr(all="ignore")
    fit = np.load(a.fit)
    full = fit["full"]
    plane = fit["plane"]
    run = int(fit["run"])

    # absolute gain of each channel's fitted response: response peak (ADC) / (Q * ADC/mV)
    tf = np.linspace(-1, 15, 1601)
    peak_adc = np.array([np.nanmax(paramscoldelec(tf, 1.0, *p[2:])) * p[1] if np.all(np.isfinite(p)) else np.nan
                         for p in full])
    if a.charge_fC is None:
        q = np.nanmedian(peak_adc) / (G_IDEAL * ADC_PER_MV)
        print(f"charge not given: using Q = {q:.2f} fC (median gain -> {G_IDEAL} mV/fC)")
    else:
        q = a.charge_fC
    gain = peak_adc / (q * ADC_PER_MV)
    # paramscoldelec(gain) is normalised so that its ideal limit peaks at ~gain; rescale so the
    # fitted response really peaks at gain_i (mV/fC) for 1 fC
    shape_peak = np.array([np.nanmax(paramscoldelec(tf, 1.0, *p[2:])) if np.all(np.isfinite(p)) else np.nan
                           for p in full])
    G["gain"] = gain / shape_peak
    sfx = ""
    if a.chresp:
        _, P = load_chresp(a.chresp)
        full = np.full_like(full, np.nan)
        full[P["ch"], 2:] = np.stack([P["tp"], P["k3"], P["k4"], P["k5"], P["k6"]], axis=1)
        full[P["ch"], :2] = 0.0
        full[P["ch"][P["k4"] == P["k6"]]] = np.nan          # k4 == k6: NaN response, skip
        G["gain"] = np.full(full.shape[0], np.nan)
        G["gain"][P["ch"]] = P["gain"]
        gain = G["gain"] * shape_peak_of(full)
        sfx = "_chresp"
        print(f"using response file {os.path.basename(a.chresp)} (absolute gains from the file)")
    G["full"] = full

    with h5py.File(a.hdf5, "r") as h5:
        rec = list_records(h5)[a.record]
        adc, _ = decode_record(h5, rec, load_channel_map())
    sub = adc.astype(float)
    sub -= np.median(sub, axis=1)[:, None]
    G["sub"] = sub

    # --- correction spectrum ---------------------------------------------------
    n = sub.shape[1]
    f = np.fft.rfftfreq(n, TICK) * 1000   # kHz
    tc = T0 + (np.arange(n) + 0.5) * TICK
    elec = np.fft.rfft(coldelec(tc, G_IDEAL, TP_IDEAL))
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for i, ch in enumerate(a.channels):
        c = np.zeros(n)
        c[:NSAMP] = channel_response(full[ch], G["gain"][ch])
        ax.semilogx(f[1:], np.abs(elec / np.fft.rfft(c))[1:], color=f"C{i}",
                    label=f"ch {ch} ({PLANES[plane[ch]]}), gain {gain[ch]:.2f} mV/fC")
    ax.axhline(1, color="k", lw=0.5)
    ax.set_ylim(0, 3)
    ax.set_xlabel("frequency [kHz]")
    ax.set_ylabel("|E_ideal(f) / E_channel(f)|")
    ax.set_title("Correction factor per frequency, response from " + (os.path.basename(a.chresp).replace("protodunevd-params-channel-response-", "file ") if a.chresp else f"run {run} fit"), fontsize=9)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, f"p6_correction_spectrum{sfx}.png"), dpi=130)

    # --- before / after on raw waveforms -------------------------------------------
    fig, axs = plt.subplots(2, 3, figsize=(13, 7))
    tt = np.linspace(-8, 60, 3000)
    for j, ch in enumerate(a.channels):
        w = sub[ch]
        wc = correct(w, full[ch], G["gain"][ch])
        p0 = np.where((w[1:-1] > w[:-2]) & (w[1:-1] >= w[2:]) & (w[1:-1] > 0.5 * w.max()))[0][0] + 1
        sl = slice(p0 - 15, p0 + 120)
        x = (np.arange(sl.start, sl.stop) - p0) * TICK
        yb, ya = w[sl], wc[sl]
        fa = ideal_fit(np.concatenate([ya[:115]]))
        for row in range(2):
            ax = axs[row, j]
            ax.plot(x, yb, "-", color="0.5", lw=1, label="before (raw ADC − pedestal)")
            ax.plot(x, ya, "-", color="C3", lw=1.2, label="after E_ideal/E_channel")
            ax.plot(tt, fa[1] * coldelec(tt - fa[0], 1.0, fa[2]), "k:", lw=1,
                    label=f"ideal shape fitted to 'after': tp={fa[2]:.2f} µs")
            ax.axhline(0, color="k", lw=0.4)
        axs[0, j].set_xlim(-8, 20)
        axs[0, j].set_title(f"run {run} {rec} ch {ch} ({PLANES[plane[ch]]})", fontsize=9)
        axs[0, j].legend(fontsize=6.5)
        axs[1, j].set_ylim(-0.025 * yb.max(), 0.025 * yb.max())
        axs[1, j].set_title("tail zoom ±2.5 % of peak", fontsize=9)
        axs[1, j].set_xlabel("time from peak [µs]")
    axs[0, 0].set_ylabel("ADC")
    axs[1, 0].set_ylabel("ADC")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, f"p6_before_after{sfx}.png"), dpi=130)

    # --- all channels: tp and gain before/after --------------------------------------
    with Pool(a.nproc) as pool:
        res = pool.map(work, range(sub.shape[0]), chunksize=32)
    r = np.array([x[1:] for x in res], dtype=float)
    ok = np.all(np.isfinite(r), axis=1)
    tpb, tpa, hb, ha = r[ok].T
    gb, ga = hb / (q * ADC_PER_MV), ha / (q * ADC_PER_MV)
    fig, axs = plt.subplots(1, 2, figsize=(11, 4))
    axs[0].hist(tpb, bins=140, range=(1.6, 2.5), histtype="step", color="r",
                label=f"before: median {np.median(tpb):.3f}, width {hw(tpb):.3f} µs")
    axs[0].hist(tpa, bins=140, range=(1.6, 2.5), histtype="step", color="b",
                label=f"after: median {np.median(tpa):.3f}, width {hw(tpa):.3f} µs")
    axs[0].set_xlabel("peaking time from ideal fit, tp [µs]")
    axs[1].hist(gb, bins=160, range=(6, 10), histtype="step", color="r",
                label=f"before: median {np.median(gb):.2f}, width {hw(gb):.3f}")
    axs[1].hist(ga, bins=160, range=(6, 10), histtype="step", color="b",
                label=f"after: median {np.median(ga):.2f}, width {hw(ga):.3f}")
    axs[1].set_xlabel(f"pulse height / Q  [mV/fC]  (Q = {q:.1f} fC assumed)")
    for ax in axs:
        ax.set_ylabel("channels")
        ax.legend(fontsize=8)
    fig.suptitle(f"run {run}, {ok.sum()} channels: before vs after the per-channel correction "
                 "(width = half of the 16–84 % interval)")
    fig.tight_layout()
    fig.savefig(os.path.join(a.outdir, f"p6_before_after_tp{sfx}.png"), dpi=130)
    print(f"{ok.sum()} channels. tp before {np.median(tpb):.3f} (width {hw(tpb):.3f}), "
          f"after {np.median(tpa):.3f} (width {hw(tpa):.3f}) us")
    print(f"height/Q before {np.median(gb):.3f} (width {hw(gb):.3f}), after {np.median(ga):.3f} "
          f"(width {hw(ga):.3f}) mV/fC;  |after-median| > 0.2: {np.sum(np.abs(ga - np.median(ga)) > 0.2)} channels")


if __name__ == "__main__":
    main()
