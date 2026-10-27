#!/usr/bin/env python3
"""Decode ProtoDUNE-VD bottom-drift (BDE, WIBEth) raw HDF5 into ADC waveforms.

No LArSoft needed: reads the DAQ HDF5 directly with h5py + numpy.

Layout of one TPC fragment (dataset RawData/Detector_Readout_0x...._WIBEth):
    72-byte fragment header, then N WIBEth frames of 7200 bytes each.
One WIBEth frame:
    word0      DAQEthHeader  (version:6 det_id:6 crate:10 slot:4 stream:12 ...)
    word1      timestamp     (62.5 MHz clock = 16 ns ticks; one ADC sample = 32 ticks = 512 ns)
    word2,3    WIBEthHeader
    then 64 time samples x 14 uint64 words: 64 channels x 14-bit ADC, packed
    little-endian, channel c in bits [14c, 14c+14) of each 896-bit block.

The (crate, slot, stream, stream-channel) -> offline channel map is the official
PD2VDTPCChannelMap_v2.txt shipped with duneprototypes (columns: offline, det,
crp, crate, slot, stream, streamchan, plane, chan_in_plane, femb, asic, asicchan).

Usage as a module:
    from pdvd_hdf5_decode import decode_record, list_records
    adc, ts0 = decode_record(h5file, record_name)   # adc: (6144, nticks) int16, row = offline channel
Usage from the command line (quick summary):
    python3 pdvd_hdf5_decode.py FILE.hdf5 [--record N]
"""
import argparse
import os

import h5py
import numpy as np

FRAG_HDR = 72
FRAME = 7200
FRAME_HDR = 32
NCH = 64
NT = 64
DEFAULT_MAP = ("/cvmfs/dune.opensciencegrid.org/products/dune/duneprototypes/"
               "v10_26_00d05/config_data/PD2VDTPCChannelMap_v2.txt")
_WEIGHTS = (1 << np.arange(14)).astype(np.int32)


def load_channel_map(path=None):
    """Return dict (crate, slot, stream) -> array[64] of offline channels, plus plane lookup."""
    path = path or os.environ.get("PDVD_CHANMAP", DEFAULT_MAP)
    m = np.loadtxt(path, dtype=np.int64)
    lut = {}
    for off, det, crp, crate, slot, stream, sch, plane, *_ in m:
        lut.setdefault((crate, slot, stream), np.full(NCH, -1, dtype=np.int64))[sch] = off
    plane = np.full(m[:, 0].max() + 1, -1, dtype=np.int8)
    plane[m[:, 0]] = m[:, 7]
    return lut, plane


def unpack_frames(raw):
    """raw: bytes of the frames only. Returns (adc[64ch, nframes*64], header word0, first timestamp)."""
    nfr = len(raw) // FRAME
    buf = np.frombuffer(raw, dtype=np.uint8, count=nfr * FRAME).reshape(nfr, FRAME)
    w0 = int.from_bytes(buf[0, 0:8].tobytes(), "little")
    ts0 = int.from_bytes(buf[0, 8:16].tobytes(), "little")
    payload = buf[:, FRAME_HDR:].reshape(nfr * NT, NCH * 14 // 8)   # (time, 112 bytes)
    bits = np.unpackbits(payload, axis=1, bitorder="little").reshape(nfr * NT, NCH, 14)
    adc = (bits.astype(np.int32) @ _WEIGHTS).astype(np.int16)        # (time, ch)
    return adc.T, w0, ts0


def header_ids(w0):
    return (w0 >> 12) & 0x3FF, (w0 >> 22) & 0xF, (w0 >> 26) & 0xFFF   # crate, slot, stream


def list_records(h5):
    return sorted(k for k in h5.keys() if k.startswith("TriggerRecord"))


def decode_record(h5, rec, chanmap=None, nchan=6144):
    """Decode every WIBEth link of one trigger record into an (nchan, nticks) array."""
    lut, _ = chanmap or load_channel_map()
    g = h5[rec]["RawData"]
    out, ts = None, None
    for name in g:
        if not name.endswith("WIBEth"):
            continue
        raw = g[name][:, 0].tobytes()[FRAG_HDR:]
        adc, w0, ts0 = unpack_frames(raw)
        key = header_ids(w0)
        if key not in lut:
            continue
        if out is None:
            out = np.zeros((nchan, adc.shape[1]), dtype=np.int16)
            ts = ts0
        n = min(out.shape[1], adc.shape[1])
        offl = lut[key]
        ok = (offl >= 0) & (offl < nchan)
        out[offl[ok], :n] = adc[ok, :n]
    return out, ts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hdf5")
    ap.add_argument("--record", type=int, default=0, help="index of trigger record (default 0)")
    a = ap.parse_args()
    with h5py.File(a.hdf5, "r") as h5:
        recs = list_records(h5)
        print(f"run {h5.attrs.get('run_number')}  stream '{h5.attrs.get('offline_data_stream')}'  "
              f"{len(recs)} trigger records")
        adc, ts = decode_record(h5, recs[a.record])
    ped = np.median(adc, axis=1)
    amp = adc.max(axis=1) - ped
    print(f"{recs[a.record]}: {adc.shape[0]} channels x {adc.shape[1]} ticks (512 ns), first ts {ts}")
    print(f"median pedestal {np.median(ped):.0f} ADC, median max-above-pedestal {np.median(amp):.0f} ADC")


if __name__ == "__main__":
    main()
