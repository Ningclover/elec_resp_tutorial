#!/bin/bash
# Regenerate every plot of the electronics-response tutorial.
#
# Usage:
#   ./make_all_plots.sh <pulser.hdf5> <response.json.bz2> [gainscan_daq.txt gainscan_raw.txt] [wire-cell-toolkit]
#
#   pulser.hdf5        a PDVD bottom-drift pulser run, e.g. np02vd_raw_run045270_..._dw_0_20260929T175649.hdf5
#   response.json.bz2  a fitted per-channel response file, e.g.
#                      protodunevd-params-channel-response-41603-no_avg_3.json.bz2
#   gainscan_*.txt     outputs of calib_chain/wf_gain_scan_ours.C (default: ../data/)
#   wire-cell-toolkit  source tree, only for the formula cross-check (optional)
#
# Needs python3 with numpy, scipy, matplotlib, h5py and /cvmfs (for the PDVD channel
# map; or set PDVD_CHANMAP=/path/to/PD2VDTPCChannelMap_v2.txt).  Takes ~3 min on 32 cores.
set -euo pipefail
HDF5=${1:?usage: $0 <pulser.hdf5> <response.json.bz2> [gainscan_daq.txt gainscan_raw.txt] [wire-cell-toolkit]}
CHRESP=${2:?need a response file}
HERE=$(cd "$(dirname "$0")" && pwd)
DATA=$HERE/../data
PLOTS=$HERE/../plots
DAQ=${3:-$DATA/gainscan_vd_ours_daq.txt}
RAW=${4:-$DATA/gainscan_vd_ours_raw.txt}
WCT=${5:-}
NPROC=${NPROC:-32}
mkdir -p "$DATA" "$PLOTS"
cd "$HERE"

RUN=$(python3 -c "import h5py,sys; print(int(h5py.File(sys.argv[1],'r').attrs['run_number']))" "$HDF5")
echo "== run $RUN"

if [[ -n "$WCT" ]]; then
  echo "== check python formulas against the toolkit C++"
  ./check_formula_vs_wct.sh "$WCT" | tee "$PLOTS/check_formula_vs_wct.txt"
fi
echo "== page 1: ideal response";            python3 p1_ideal_response.py --outdir "$PLOTS"
echo "== page 2: pole-zero tail";            python3 p2_pole_zero_tail.py "$CHRESP" --outdir "$PLOTS"
echo "== page 3: raw pulser data";           python3 p3_raw_pulser.py "$HDF5" --outdir "$PLOTS"
echo "== page 4: average pulses + fits"
python3 make_avg_pulses.py "$HDF5" -o "$DATA/avg_pulses_run$RUN.npz"
python3 p4_fit_pulses.py "$DATA/avg_pulses_run$RUN.npz" --chresp "$CHRESP" --tpref "$DAQ" \
        --nproc "$NPROC" --outdir "$PLOTS"
echo "== page 5: linearity";                 python3 p5_linearity.py "$DAQ" "$RAW" --outdir "$PLOTS"
echo "== page 6: apply the correction"
python3 p6_apply_correction.py "$HDF5" "$DATA/fit_run$RUN.npz" --nproc "$NPROC" --outdir "$PLOTS"
python3 p6_apply_correction.py "$HDF5" "$DATA/fit_run$RUN.npz" --chresp "$CHRESP" --nproc "$NPROC" --outdir "$PLOTS"
echo "== done: plots in $PLOTS"
