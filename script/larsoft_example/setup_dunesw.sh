# source this INSIDE the SL7 container:   source setup_dunesw.sh [VERSION] [QUALIFIER]
# defaults: v10_26_00d05 e26:prof
DUNESW_VER=${1:-${DUNESW_VER:-v10_26_00d05}}
DUNESW_QUAL=${2:-${DUNESW_QUAL:-e26:prof}}
# UPS needs a writable TMPDIR; a host TMPDIR is usually not mounted in the container
if [[ -z ${TMPDIR:-} || ! -w ${TMPDIR:-/nonexistent} ]]; then export TMPDIR=/tmp; fi
source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh > /dev/null
setup dunesw "$DUNESW_VER" -q "$DUNESW_QUAL"
echo "dunesw $DUNESW_VER -q $DUNESW_QUAL"
echo "  lar          : $(command -v lar)"
echo "  wire-cell    : $(command -v wire-cell)"
echo "  DUNERECO_DIR : $DUNERECO_DIR"
echo "  release cfg  : $DUNERECO_DIR/wire-cell-cfg   (first on WIRECELL_PATH)"
