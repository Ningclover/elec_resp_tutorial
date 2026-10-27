# env.sh -- sourced by run_nfsp.sh and plot/dump_wires.sh.
#
# enter_container "$0" "$@" : if not already in an SL7 container, re-exec the
#                             calling script inside one (no-op inside SL7).
# setup_dunesw              : source setup_dune.sh + setup dunesw.
#
# Override with environment variables:
#   DUNESW_VER     (default v10_26_00d05)
#   DUNESW_QUAL    (default e26:prof)
#   SL7_IMAGE      (default the Fermilab SL7 dev image on cvmfs)
#   APPTAINER      (default: apptainer on PATH, else the cvmfs build)
#   CONTAINER_HOME (default $HOME)
#   EXTRA_BINDS    comma-separated extra bind mounts, e.g. /data,/scratch

# bundle root = the directory holding this file; always bound in the container
NFSP_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

DUNESW_VER=${DUNESW_VER:-v10_26_00d05}
DUNESW_QUAL=${DUNESW_QUAL:-e26:prof}
export DUNESW_VER DUNESW_QUAL

enter_container() {
  [[ -f /etc/redhat-release ]] && return 0
  local script=$1; shift
  local app=${APPTAINER:-$(command -v apptainer || echo /cvmfs/oasis.opensciencegrid.org/mis/apptainer/current/bin/apptainer)}
  local image=${SL7_IMAGE:-/cvmfs/singularity.opensciencegrid.org/fermilab/fnal-dev-sl7:latest}
  # bind cvmfs, the bundle, the current dir, and the parent directory of
  # every argument that is a path (input file, output dir, not-yet-written
  # output file, ...)
  local binds="/cvmfs,$NFSP_ROOT,$PWD"
  local a
  for a in "$@"; do
    [[ $a == */* || -e $a ]] && [[ -d $(dirname "$a") ]] \
      && binds="$binds,$(cd "$(dirname "$a")" && pwd)"
  done
  [[ -n ${EXTRA_BINDS:-} ]] && binds="$binds,$EXTRA_BINDS"
  exec "$app" exec --no-home -H "${CONTAINER_HOME:-$HOME}" -B "$binds" --ipc --pid \
       "$image" /bin/bash "$script" "$@"
}

setup_dunesw() {
  # a host TMPDIR is usually not mounted in the container; UPS needs a
  # writable one or every setup fails with "Read-only file system"
  if [[ -z ${TMPDIR:-} || ! -w ${TMPDIR:-/nonexistent} ]]; then
    export TMPDIR=/tmp
  fi
  set +u   # UPS setup scripts are not set -u clean
  source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh > /dev/null
  setup dunesw "$DUNESW_VER" -q "$DUNESW_QUAL"
  set -u
  if [[ -z ${DUNERECO_DIR:-} ]] || ! command -v lar > /dev/null; then
    echo "setup dunesw $DUNESW_VER -q $DUNESW_QUAL failed" >&2
    exit 1
  fi
}
