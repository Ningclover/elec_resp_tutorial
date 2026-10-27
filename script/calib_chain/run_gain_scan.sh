#!/bin/bash
# run_gain_scan.sh -- run the per-channel gain (dA/dQ) + t_p extraction for all
# 4 detector/variant combos (vd/hd x daq/raw), then make the comparison plots.
# Wraps everything in the apptainer container.  Meant to run in tmux.
#
#   tmux new -s gainscan
#   bash run_gain_scan.sh
set -uo pipefail

ELEC=/exp/dune/data/users/xning/proto-dune-vd/elec_res
APPTAINER=/cvmfs/oasis.opensciencegrid.org/mis/apptainer/current/bin/apptainer
IMAGE=/cvmfs/singularity.opensciencegrid.org/fermilab/fnal-dev-sl7:latest

LOG="$ELEC/gain_scan_run.log"
: > "$LOG"

"$APPTAINER" exec \
  -B /cvmfs,/exp,/nashome,/opt,/etc/hostname,/etc/hosts,/etc/krb5.conf \
  --ipc --pid "$IMAGE" /bin/bash -c '
    set +u
    source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh
    setup dunesw v10_20_08d00 -q e26:prof
    set -u
    cd '"$ELEC"'
    mkdir -p plots root_files txt_files
    echo "=== compiling + running gain scan: $(date) ==="
    for v in daq raw; do
      for d in vd hd; do
        echo ""
        echo "############################## $d $v ##############################"
        root -l -b -q "wf_gain_scan.C+(\"$d\",\"$v\")" 2>&1 \
          | grep -vE "Info in <|Warning in <T(Canvas|File)"
      done
    done
    echo ""
    echo "=== making plots: $(date) ==="
    for d in vd hd; do
      echo "----- plot $d -----"
      root -l -b -q "plot_gain_tp.C(\"$d\")" 2>&1 | grep -vE "Info in <|Warning in <"
    done
    echo "=== ALL DONE: $(date) ==="
  ' 2>&1 | tee "$LOG"
