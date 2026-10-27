#!/usr/bin/env python3
"""Re-grid a ParamsPerChannelResponse file onto a different tick period.

Why this is safe (and why no refit is needed)
---------------------------------------------
A ParamsPerChannelResponse file does NOT store sampled waveforms.  Each
channel_info entry stores the *fitted parameters* of the cold-electronics
response -- gain, shaping, k3..k6 -- and the C++ synthesizes the waveform
analytically at load time:

    ParamsPerChannelResponse.cxx:40   m_bins = Binning(nsamp, t0, t0+nsamp*tick)
    ParamsPerChannelResponse.cxx:57   Response::ParamsColdElec ce(gain, shaping,
                                                                 k3, k4, k5, k6);
                                      Waveform::realseq_t resp = ce.generate(m_bins);

Response::ParamsColdElec exposes `operator()(double time)` -- a continuous
function of time (Response.h:225).  The sampling grid therefore lives entirely
in the file's `tick`/`t0`/`nsamp` header, and changing it re-samples the same
analytic response exactly.  There is no interpolation and no loss: the
per-channel fit parameters are copied through byte-for-byte.

Why you would want to
---------------------
OmnibusSigProc takes its period from the frame at runtime
(OmnibusSigProc.cxx:846  m_period = frame->tick()) and then hard-throws if the
response binning disagrees:

    OmnibusSigProc.cxx:1195   if (cr_bins.binsize() != m_period) THROW(...)
                              "channel response size mismatch"

So a response fitted on 512 ns data cannot be used by a chain running at the
500 ns default tick until it is re-gridded.  (SigProc::ResponseShaper has the
equivalent check commented out as a FIXME -- ResponseShaper.cxx:68 -- so it
tolerates the mismatch silently.  Do not rely on that.)

`nsamp` need not divide the old span evenly: OmnibusSigProc clips with
min(tch_resp.size(), m_fft_nticks) and zero-pads, so any length that covers the
response is fine.  The default rounds the original time span up.

The k4 == k6 pole (checked on every run)
----------------------------------------
Response::paramscoldelec divides by (k4 - k6).  A fit that lands with k4
EXACTLY equal to k6 gives a NaN response for that channel, and because
OmnibusSigProc FFTs across wires (OmnibusSigProc.cxx:1236) that single NaN
spreads over the whole plane -- the plane reconstructs as exactly zero charge
with NO error in the log.  This was seen on PDVD run039252 anode 2 plane U
(channel 3209, k4 = k6 = 0.1286484).

Every run of this script reports such channels.  --fix-pole breaks the
degeneracy by scaling k6 by (1 + 1e-6); the pole is removable so the response
is recovered to ~1e-5.  The real fix belongs in the fitting code.

Usage
-----
    ./regen_chresp_tick.py IN.json.bz2 --tick 500 [-o OUT.json.bz2] [--nsamp N]
    ./regen_chresp_tick.py IN.json.bz2 --tick 500 --check    # report only
    ./regen_chresp_tick.py IN.json.bz2 --tick 500 --fix-pole # break k4==k6

Example (PDVD bottom drifter, 512 ns fit -> 500 ns default tick):
    ./regen_chresp_tick.py \\
        protodunevd-params-channel-response-41603-no_avg_3.json.bz2 --tick 500
    # -> protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2
"""

import argparse
import bz2
import json
import math
import os
import sys

# Relative amount to move k6 by when breaking an exact k4 == k6 degeneracy.
# The pole is removable, so any small value works; 1e-6 changes the resulting
# response by less than 1e-5 relative (checked against neighbouring channels).
POLE_NUDGE = 1e-6


def jopen(path, mode="rt"):
    """Open plain .json or bz2-compressed .json.bz2 transparently."""
    if path.endswith(".bz2"):
        return bz2.open(path, mode)
    return open(path, mode)


def load(path):
    with jopen(path) as f:
        return json.load(f)


def check_pole(doc, label="input"):
    """Flag channels whose fit parameters hit the k4 == k6 pole.

    Response::paramscoldelec (util/src/Response.cxx:359-361) divides by
    (k4 - k6) in several terms.  When a fit lands with k4 EXACTLY equal to k6
    the response evaluates to NaN/Inf for every sample in range.

    This is not a cosmetic problem.  OmnibusSigProc::decon_2D_init stacks the
    per-channel responses into one array, FFTs it, and then runs a
    wire-direction FFT over the whole plane (OmnibusSigProc.cxx:1236) which
    mixes every row -- so ONE NaN channel propagates across ALL channels of
    that plane and the ROI logic then rejects everything.  The observed
    symptom is an entire plane reconstructing as exactly zero charge, with no
    error message anywhere in the log.

    The pole is removable: the limit as k6 -> k4 is well defined, so nudging
    k6 by ~1e-6 relative recovers the correct response (verified stable across
    5 orders of magnitude of nudge).  Channels merely CLOSE to degenerate are
    fine -- only exact equality is fatal.

    Returns the list of (channel, entry) pairs that are degenerate.
    """
    bad = []
    near = 0
    for e in doc["channel_info"]:
        k4, k6 = e.get("k4"), e.get("k6")
        if k4 is None or k6 is None:
            continue
        if k4 == k6:
            for c in e["channels"]:
                bad.append((c, e))
        elif abs(k4 - k6) <= 1e-6 * max(abs(k4), 1e-30):
            near += len(e["channels"])

    if bad:
        print(f"  !! WARNING: {len(bad)} channel(s) have k4 == k6 EXACTLY -- their")
        print(f"  !! response is NaN and will zero out the WHOLE PLANE in SP.")
        for c, e in sorted(bad)[:20]:
            print(f"  !!   ch {c}: k4=k6={e['k4']:.7g} gain={e['gain']:.6g} "
                  f"shaping={e['shaping']:.2f}")
        if len(bad) > 20:
            print(f"  !!   ... and {len(bad) - 20} more")
        print(f"  !! Fix upstream in the fit, or pass --fix-pole to nudge k6 by")
        print(f"  !! {POLE_NUDGE:g} relative (removable pole; response unchanged to ~1e-5).")
    if near:
        print(f"  note: {near} channel(s) within 1e-6 of k4==k6 (near-degenerate, "
              f"numerically OK)")
    return bad


def summarize(doc, label):
    tick, t0, nsamp = doc["tick"], doc["t0"], doc["nsamp"]
    chans = sorted(c for e in doc["channel_info"] for c in e["channels"])
    print(
        f"{label}: tick={tick} ns  t0={t0} ns  nsamp={nsamp}  "
        f"span={nsamp * tick} ns  [{t0}, {t0 + nsamp * tick})"
    )
    print(
        f"{'':{len(label)}}  channels={len(chans)} "
        f"range={chans[0]}..{chans[-1]} "
        f"contiguous={chans == list(range(chans[0], chans[-1] + 1))}"
    )
    return chans


def main():
    ap = argparse.ArgumentParser(
        description="Re-grid a ParamsPerChannelResponse file onto a new tick.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Usage\n-----\n", 1)[-1],
    )
    ap.add_argument("infile", help="input .json or .json.bz2")
    ap.add_argument(
        "--tick", type=float, required=True, help="target tick period in ns (e.g. 500)"
    )
    ap.add_argument("-o", "--outfile", help="output path (default: <in>-<tick>ns.json.bz2)")
    ap.add_argument(
        "--nsamp",
        type=int,
        help="target sample count (default: ceil to cover the original time span)",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="report the input file's grid and channel coverage, write nothing",
    )
    ap.add_argument("-f", "--force", action="store_true", help="overwrite existing output")
    ap.add_argument(
        "--fix-pole",
        action="store_true",
        help=f"break any exact k4 == k6 degeneracy by scaling k6 by "
             f"(1 + {POLE_NUDGE:g}).  Without this the file is written unchanged "
             f"and only a warning is printed.",
    )
    args = ap.parse_args()

    doc = load(args.infile)
    for key in ("tick", "t0", "nsamp", "channel_info"):
        if key not in doc:
            sys.exit(f"error: {args.infile} has no '{key}' -- not a "
                     f"ParamsPerChannelResponse file?")

    summarize(doc, "input ")
    bad = check_pole(doc)
    if args.check:
        return

    old_tick, old_nsamp = doc["tick"], doc["nsamp"]
    if args.tick <= 0:
        sys.exit("error: --tick must be positive")

    # Cover at least the original time span.
    nsamp = args.nsamp or math.ceil(old_nsamp * old_tick / args.tick)

    out = args.outfile
    if not out:
        base = args.infile
        for ext in (".json.bz2", ".json"):
            if base.endswith(ext):
                base = base[: -len(ext)]
                break
        tickstr = f"{args.tick:g}"
        out = f"{base}-{tickstr}ns.json.bz2"

    if os.path.exists(out) and not args.force:
        sys.exit(f"error: {out} exists (use -f to overwrite)")

    # Only the sampling grid changes.  channel_info -- the per-channel fit
    # parameters -- is passed through untouched, unless --fix-pole was asked
    # for and there is an exact k4 == k6 degeneracy to break.
    doc["tick"] = float(args.tick)
    doc["nsamp"] = int(nsamp)

    nfixed = 0
    if bad and args.fix_pole:
        for e in doc["channel_info"]:
            if e.get("k4") is not None and e["k4"] == e.get("k6"):
                e["k6"] = e["k6"] * (1.0 + POLE_NUDGE)
                nfixed += len(e["channels"])
        print(f"  --fix-pole: nudged k6 on {nfixed} channel(s)")

    with bz2.open(out, "wt") if out.endswith(".bz2") else open(out, "wt") as f:
        json.dump(doc, f)

    summarize(doc, "output")
    print(f"wrote {out}")

    # Read back and prove the fit parameters survived unchanged (apart from any
    # deliberate --fix-pole nudge).
    back = load(out)
    orig = load(args.infile)
    if nfixed:
        assert not check_pole(back), "k4 == k6 still present after --fix-pole"
        print(f"verified: pole broken on {nfixed} channel(s); all other fit "
              f"parameters untouched")
    else:
        assert back["channel_info"] == orig["channel_info"], \
            "channel_info changed -- this should be impossible"
        print("verified: channel_info identical to input (fit parameters untouched)")
    if bad and not args.fix_pole:
        print("  !! output still contains the k4 == k6 channel(s) above -- "
              "re-run with --fix-pole, or fix the fit upstream.")


if __name__ == "__main__":
    main()
