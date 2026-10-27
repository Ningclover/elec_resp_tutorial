#!/usr/bin/env python3
"""Average the positive pulser pulses of every channel in a raw HDF5 file.

For each channel and each trigger record: subtract the median (pedestal), find
positive peaks above half the maximum, cut [peak-BEFORE, peak+AFTER) ticks, and
average all of them.  Saves an .npz with

    avg   (nchan, BEFORE+AFTER) float  average pulse, ADC above pedestal
    npulse(nchan,)  int                pulses averaged
    ped   (nchan,)  float              median pedestal of the first record
    pkrms (nchan,)  float              pulse-to-pulse RMS of the peak height
    plane (nchan,)  int                0=U 1=V 2=W
    before, after, tick_us, run

AFTER defaults to 200 ticks (102 us): the tail lasts longer than the 100 ticks
used by the ROOT fitter, and the next (negative) pulser edge is ~270 ticks later.

Usage:  python3 make_avg_pulses.py FILE.hdf5 [-o avg_pulses_runNNNNN.npz] [--nrec N]
"""
import argparse
import os

import h5py
import numpy as np

from pdvd_hdf5_decode import decode_record, list_records, load_channel_map


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hdf5")
    ap.add_argument("-o", "--output", default=None)
    ap.add_argument("--nrec", type=int, default=0)
    ap.add_argument("--before", type=int, default=15)
    ap.add_argument("--after", type=int, default=200)
    a = ap.parse_args()
    cmap = load_channel_map()
    B, A = a.before, a.after
    with h5py.File(a.hdf5, "r") as h5:
        run = int(h5.attrs["run_number"])
        recs = list_records(h5)
        if a.nrec:
            recs = recs[:a.nrec]
        tot = n = pk = pk2 = ped0 = None
        for ir, r in enumerate(recs):
            adc, _ = decode_record(h5, r, cmap)
            w = adc.astype(np.float32)
            ped = np.median(w, axis=1)
            w -= ped[:, None]
            if tot is None:
                nch, nt = w.shape
                tot = np.zeros((nch, B + A))
                n = np.zeros(nch, dtype=int)
                pk = np.zeros(nch)
                pk2 = np.zeros(nch)
                ped0 = ped.copy()
            mx = w.max(axis=1)
            loc = (w[:, 1:-1] > w[:, :-2]) & (w[:, 1:-1] >= w[:, 2:]) & (w[:, 1:-1] > 0.5 * mx[:, None])
            chs, ts = np.nonzero(loc)
            ts = ts + 1
            ok = (ts - B >= 0) & (ts + A < nt) & (mx[chs] > 200)
            for ch, t in zip(chs[ok], ts[ok]):
                seg = w[ch, t - B:t + A]
                tot[ch] += seg
                n[ch] += 1
                pk[ch] += seg[B]
                pk2[ch] += seg[B] ** 2
            print(f"{r}: {ok.sum()} pulses", flush=True)
    with np.errstate(invalid="ignore", divide="ignore"):
        avg = tot / n[:, None]
        pkm = pk / n
        pkrms = np.sqrt(np.maximum(pk2 / n - pkm ** 2, 0))
    out = a.output or f"avg_pulses_run{run}.npz"
    np.savez_compressed(out, avg=avg, npulse=n, ped=ped0, pkrms=pkrms,
                        plane=cmap[1][:avg.shape[0]], before=B, after=A, tick_us=0.512, run=run)
    print(f"wrote {out}: {avg.shape[0]} channels, median {np.median(n):.0f} pulses/channel, "
          f"{(n == 0).sum()} channels without pulses")


if __name__ == "__main__":
    main()
