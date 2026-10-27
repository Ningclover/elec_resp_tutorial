#!/bin/bash
# The whole manual recipe in one go, started from the HOST:
#   enter the SL7 container -> setup dunesw -> copy+edit wire-cell-cfg -> run my_nfsp.fcl
#
# Usage:  ./run_example.sh INPUT.hdf5 RESPONSE-500ns.json.bz2 [NEVTS] [WORKDIR]
#   WORKDIR default ./larsoft_example_run ; output: WORKDIR/*_mynfsp.root, WORKDIR/lar.log
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
if [[ ! -f /etc/redhat-release ]]; then                  # on the host: re-exec inside SL7
  IN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
  RESP=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
  WORK=${4:-$PWD/larsoft_example_run}; mkdir -p "$WORK"; WORK=$(cd "$WORK" && pwd)
  APP=${APPTAINER:-$(command -v apptainer || echo /cvmfs/oasis.opensciencegrid.org/mis/apptainer/current/bin/apptainer)}
  IMAGE=${SL7_IMAGE:-/cvmfs/singularity.opensciencegrid.org/fermilab/fnal-dev-sl7:latest}
  exec "$APP" exec --no-home -H "${CONTAINER_HOME:-$HOME}" \
       -B "/cvmfs,$HERE,$WORK,$(dirname "$IN"),$(dirname "$RESP")" --ipc --pid "$IMAGE" \
       /bin/bash "$0" "$IN" "$RESP" "${3:-1}" "$WORK"
fi
IN=$1; RESP=$2; NEVTS=$3; WORK=$4
set +u; source "$HERE/setup_dunesw.sh" "${DUNESW_VER:-v10_26_00d05}" "${DUNESW_QUAL:-e26:prof}"; set -u
cd "$WORK" || exit 1
[[ -d my-wire-cell-cfg ]] || "$HERE/make_cfg_copy.sh" my-wire-cell-cfg "$RESP" || exit 1
export WIRECELL_PATH=$WORK/my-wire-cell-cfg:$WIRECELL_PATH
export FHICL_FILE_PATH=$HERE:$FHICL_FILE_PATH
lar -c my_nfsp.fcl --debug-config my_nfsp_expanded.fcl > /dev/null 2>&1
lar -n "$NEVTS" -c my_nfsp.fcl "$IN" > lar.log 2>&1
rc=$?
echo "lar exit $rc;  correction applied $(grep -c 'applying ch-by-ch electronics response correction' lar.log) times" \
     "(expect NEVTS x 4 anodes x 3 planes)"
grep -o "protodunevd-params-channel-response[^ \"']*" lar.log | sort -u
exit $rc
