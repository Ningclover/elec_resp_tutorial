#!/bin/bash
# Run a script inside the Fermilab SL7 container (needs /cvmfs + apptainer).
#   ./launch_container.sh <script> [args...]
# Add extra bind dirs (where your data lives) with EXTRA_BINDS=/path1,/path2
SCRIPT="$1"; shift
BINDS=/cvmfs,/etc/hostname,/etc/hosts,"$PWD"
[ -n "$EXTRA_BINDS" ] && BINDS="$BINDS,$EXTRA_BINDS"
apptainer exec -B "$BINDS" --ipc --pid \
  /cvmfs/singularity.opensciencegrid.org/fermilab/fnal-dev-sl7:latest \
  /bin/bash "$SCRIPT" "$@"
