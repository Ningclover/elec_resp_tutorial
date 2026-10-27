#!/bin/bash
#
# incontainer_elec_calib.sh -- electronics-response CALIBRATION pipeline,
# meant to be run from INSIDE the apptainer container.
#
#   1. enter the container first:   bash /nashome/x/xning/container.sh
#   2. then run this script:        bash incontainer_elec_calib.sh --det vd --run 41602 --from-hdf5 ...
#
# It sets up dunesw + the locally-built WireCell toolkit, then runs:
#   1. lar  : WireCell NF with the per-channel perChannelShaper correction
#   2. root : dump corrected RawDigits -> magnify TH2 ("h_raw")
#   3. root : fit each channel with the ideal cold-electronics response
#
# Steps can be limited with --steps "nf,convert,fit" (default: all).
#
# Examples:
#   # VD from a reco_stage1 art-ROOT (pre-decoded RawDigits):
#   bash incontainer_elec_calib.sh --det vd --run 41573 \
#       --input /exp/dune/data/users/ktellez/wf_correction/np02vd_raw_run041573_..._keepup.root
#
#   # VD from raw HDF5 (decodes first):
#   bash incontainer_elec_calib.sh --det vd --run 41602 --from-hdf5 \
#       --input /exp/dune/data/users/ktellez/wf_correction/np02vd_raw_run041602_..._20260107T233130.hdf5
#
#   # HD from a reco_stage1 art-ROOT:
#   bash incontainer_elec_calib.sh --det hd --run 30413 \
#       --input /exp/dune/data/users/ktellez/wf_correction/np04hd_raw_run030413_..._reco_stage1.root

set -uo pipefail

# ---------------------------------------------------------------- config ----
PROTO=/exp/dune/data/users/xning/proto-dune-vd
ELEC=$PROTO/elec_res
WCCFG=$PROTO/wire-cell-cfg                       # copied cfg (calib jsonnets + chresp JSONs)
WCDATA=$PROTO/git/wire-cell-data                 # shared WCT data files
WCLIB=$PROTO/wire-cell-toolkit/install/lib       # locally built toolkit
DUNERECO_FCL=$PROTO/git/dunereco/dunereco/DUNEWireCell  # locally-edited wirecell_dune.fcl
DUNESW_VER=v10_20_08d00
DUNESW_QUAL=e26:prof

# ---------------------------------------------------------------- args ------
DET=""; RUN=""; INPUT=""; STEPS="nf,convert,fit"; NEVTS=1; FROM_HDF5=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --det)       DET="$2"; shift 2 ;;
    --run)       RUN="$2"; shift 2 ;;
    --input)     INPUT="$2"; shift 2 ;;
    --steps)     STEPS="$2"; shift 2 ;;
    --nevts)     NEVTS="$2"; shift 2 ;;
    --from-hdf5) FROM_HDF5=1; shift ;;
    -h|--help)   grep '^#' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

[[ -z "$DET" || -z "$RUN" || -z "$INPUT" ]] && { echo "ERROR: --det, --run and --input are required."; exit 1; }
DET=$(echo "$DET" | tr '[:upper:]' '[:lower:]')

# det-specific bits: fcl, convert macro, magnify prefix
case "$DET" in
  hd)
    FCL=run_nf_elec_calib_hd.fcl
    CONVERT=convert_artroot_raw.C ; CONV_FN=convert_artroot_raw
    MAGPREFIX=hraw-magnify
    ;;
  vd)
    if [[ "$FROM_HDF5" == "1" ]]; then FCL=run_nf_elec_calib_vd_hdf5.fcl
    else                               FCL=run_nf_elec_calib_vd.fcl ; fi
    CONVERT=vd_convert_artroot_raw.C ; CONV_FN=vd_convert_artroot_raw
    MAGPREFIX=hraw-pdvd-magnify
    ;;
  *) echo "ERROR: --det must be hd or vd"; exit 1 ;;
esac

mkdir -p "$ELEC/output"
cd "$ELEC/output" || exit 1

# ---------------------------------------------------------------- env -------
# Assumes we are ALREADY inside the apptainer container.
echo "=== setting up dunesw + local WireCell toolkit ==="
# The UPS setup scripts are not "set -u" clean (they reference unset vars such
# as FW_SEARCH_PATH), so relax nounset just around sourcing/setup.
set +u
source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh
setup dunesw ${DUNESW_VER} -q ${DUNESW_QUAL}
set -u
export WIRECELL_PATH=${WCCFG}:${WCDATA}:${WIRECELL_PATH:-}
export LD_LIBRARY_PATH=${WCLIB}:${LD_LIBRARY_PATH:-}
export FHICL_FILE_PATH=${ELEC}:${DUNERECO_FCL}:${FHICL_FILE_PATH:-}
echo "  WIRECELL_PATH head : ${WCCFG}"
echo "  toolkit lib        : ${WCLIB}"

contains_step() { [[ ",$STEPS," == *",$1,"* ]]; }

# ---------------------------------------------------------------- 1. NF -----
if contains_step nf; then
  echo "=========================================================="
  echo "[1/3] NF + perChannelShaper  (${DET}, run ${RUN}, fcl ${FCL})"
  echo "=========================================================="
  lar -n "${NEVTS}" -c "${FCL}" "${INPUT}" 2>&1 | tee "nf_eleccalib_${DET}_${RUN}.log"
fi

# locate the produced calib art-ROOT
if [[ "$FROM_HDF5" == "1" ]]; then NFGLOB="*_eleccalib_h5.root"; else NFGLOB="*_eleccalib.root"; fi
NFFILE=$(ls -t ${ELEC}/output/${NFGLOB} 2>/dev/null | head -1)

# ---------------------------------------------------------------- 2. convert
if contains_step convert; then
  echo "=========================================================="
  echo "[2/3] Convert corrected RawDigits -> magnify TH2 (h_raw)"
  echo "=========================================================="
  [[ -z "$NFFILE" ]] && { echo "ERROR: no calib art-ROOT (${NFGLOB}) found for convert step."; exit 1; }
  echo "  input art-ROOT: $NFFILE"
  root -b -q "${ELEC}/${CONVERT}(\"${NFFILE}\")" 2>&1 | tee "convert_${DET}_${RUN}.log"
fi

# locate the magnify file
MAGFILE=$(ls -t ${ELEC}/output/${MAGPREFIX}-${RUN}-*.root 2>/dev/null | head -1)
[[ -z "$MAGFILE" ]] && MAGFILE=$(ls -t ${ELEC}/output/${MAGPREFIX}-*.root 2>/dev/null | head -1)

# ---------------------------------------------------------------- 3. fit ----
if contains_step fit; then
  echo "=========================================================="
  echo "[3/3] Fit each channel with the IDEAL cold-electronics response"
  echo "=========================================================="
  [[ -z "$MAGFILE" ]] && { echo "ERROR: no magnify file (${MAGPREFIX}-*.root) found for fit step."; exit 1; }
  echo "  input magnify: $MAGFILE"
  root -b -q "${ELEC}/wf_fitter_ideal.C(\"${MAGFILE}\",\"${DET}\",${RUN})" 2>&1 | tee "fit_${DET}_${RUN}.log"
  echo ""
  echo "Done.  Fit results:"
  echo "  ${ELEC}/output/txt_files/eleccalib_fit_${DET}_run_${RUN}.txt"
  echo "  ${ELEC}/output/root_files/eleccalib_fit_${DET}_run_${RUN}.root"
fi
