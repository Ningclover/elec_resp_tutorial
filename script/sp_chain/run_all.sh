#!/bin/bash
#
# run_all.sh INPUT.hdf5 [NEVTS] [RUNLABEL]
#
# Full before/after test: 4 NF+SP jobs in parallel ({trad,dnn} x {off,on}),
# flatten the gauss recob::Wires, and make the comparison plots.
#
#   runs/<arm>_<off|on>/   art output + nfsp.log
#   plots/                 <arm>_<off|on>.root dumps, PNGs, summary.txt
#
# The plot step needs a host python3 with numpy, uproot and matplotlib
# (PYTHON=... to choose one).  Environment knobs of run_nfsp.sh (OUTDIR,
# ACTIVE_ANODES, DUNESW_VER, ...) apply here too.

set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[[ $# -ge 1 ]] || { sed -n '3,13p' "$0"; exit 1; }
INPUT=$1; NEVTS=${2:-3}; RUNLABEL=${3:-}
export OUTDIR=${OUTDIR:-$PWD/runs}
PLOTDIR=${PLOTDIR:-$PWD/plots}
mkdir -p "$OUTDIR" "$PLOTDIR"

pids=()
for arm in trad dnn; do
  for pc in off on; do
    "$HERE/run_nfsp.sh" $arm $pc "$INPUT" "$NEVTS" &
    pids+=($!)
  done
done
rc=0
for p in "${pids[@]}"; do wait "$p" || rc=1; done
[[ $rc == 0 ]] || { echo "a job failed -- see $OUTDIR/*/nfsp.log"; exit 1; }

for arm in trad dnn; do
  for pc in off on; do
    art=$(ls "$OUTDIR/${arm}_${pc}"/*_nfsp_${arm}.root)
    "$HERE/plot/dump_wires.sh" "$art" "wclsdatavd:gauss:pdvdnfsp${arm}" \
        "$PLOTDIR/${arm}_${pc}.root" > "$PLOTDIR/dump_${arm}_${pc}.log" 2>&1 &
  done
done
wait

PYTHON=${PYTHON:-python3}
if "$PYTHON" -c "import numpy, uproot, matplotlib" 2>/dev/null; then
  "$PYTHON" "$HERE/plot/plot_pcresp.py" "$PLOTDIR" --run "$RUNLABEL" \
      --release "${DUNESW_VER:-v10_26_00d05}"
else
  echo "no python with numpy/uproot/matplotlib; run by hand:"
  echo "  python3 $HERE/plot/plot_pcresp.py $PLOTDIR --run '$RUNLABEL'"
fi
