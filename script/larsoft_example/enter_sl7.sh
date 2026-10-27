#!/bin/bash
# Open an interactive shell in the Fermilab SL7 container (needed for dunesw/UPS).
#
# Usage:  ./enter_sl7.sh [DIR ...]
#   Every DIR (your data, your work area, ...) is bind-mounted so it is visible inside.
#   /cvmfs and the current directory are always mounted.
# Then, inside:  source setup_dunesw.sh
#
# Override with: APPTAINER, SL7_IMAGE, CONTAINER_HOME
APP=${APPTAINER:-$(command -v apptainer || echo /cvmfs/oasis.opensciencegrid.org/mis/apptainer/current/bin/apptainer)}
IMAGE=${SL7_IMAGE:-/cvmfs/singularity.opensciencegrid.org/fermilab/fnal-dev-sl7:latest}
BINDS="/cvmfs,$PWD"
for d in "$@"; do BINDS="$BINDS,$(cd "$d" && pwd)"; done
exec "$APP" shell --no-home -H "${CONTAINER_HOME:-$HOME}" -B "$BINDS" --ipc --pid "$IMAGE"
