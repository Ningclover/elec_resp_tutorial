#!/bin/bash
#
# run_elec_calib.sh -- end-to-end electronics-response CALIBRATION (validation)
# for ProtoDUNE-HD and ProtoDUNE-VD.
#
# Pipeline:
#   1. lar : run the WireCell noise filter with the per-channel
#            electronics-response correction (perChannelShaper) enabled,
#            reading decoded RawDigits and writing response-equalized RawDigits.
#   2. root: dump the corrected RawDigits to a magnify TH2 ("h_raw").
#   3. root: re-fit every channel with the IDEAL cold-electronics response and
#            write per-channel fit results (t_p, A_0, chi2/dof).
#
# After a successful per-channel correction, every channel should fit the ideal
# response with chi2/dof ~ 1 and a consistent peaking time.
#
# Run this OUTSIDE the container; it wraps each step in apptainer exec itself.
#
# Usage:
#   ./run_elec_calib.sh --det hd --run 30413 \
#       --input /exp/dune/data/users/ktellez/wf_correction/np04hd_raw_run030413_0000_dataflow0_datawriter_0_20241016T220200_reco_stage1.root
#   ./run_elec_calib.sh --det vd --run 41573 \
#       --input /exp/dune/data/users/ktellez/wf_correction/np02vd_raw_run041573_0000_df-s04-d0_dw_0_20260107T223634_reco_stage1_20260510T040555_keepup.root
#
# Steps can be limited with --steps "nf,convert,fit" (default: all).

set -euo pipefail

# ---------------------------------------------------------------- config ----
PROTO=/exp/dune/data/users/xning/proto-dune-vd
ELEC=$PROTO/elec_res
WCCFG=$PROTO/wire-cell-cfg                       # our copied cfg (new calib jsonnets + JSONs)
WCDATA=$PROTO/git/wire-cell-data                 # shared WCT data files
WCLIB=$PROTO/wire-cell-toolkit/install/lib       # our locally built toolkit
DUNERECO_FCL=$PROTO/git/dunereco/dunereco/DUNEWireCell  # locally-edited wirecell_dune.fcl
DUNESW_VER=v10_20_08d00
DUNESW_QUAL=e26:prof

APPTAINER=/cvmfs/oasis.opensciencegrid.org/mis/apptainer/current/bin/apptainer
IMAGE=/cvmfs/singularity.opensciencegrid.org/fermilab/fnal-dev-sl7:latest

# ---------------------------------------------------------------- args ------
DET=""
RUN=""
INPUT=""
STEPS="nf,convert,fit"
NEVTS=1
FROM_HDF5=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --det)       DET="$2"; shift 2 ;;
    --run)       RUN="$2"; shift 2 ;;
    --input)     INPUT="$2"; shift 2 ;;
    --steps)     STEPS="$2"; shift 2 ;;
    --nevts)     NEVTS="$2"; shift 2 ;;
    --from-hdf5) FROM_HDF5=1; shift ;;
    -h|--help) grep '^#' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

[[ -z "$DET" || -z "$RUN" || -z "$INPUT" ]] && { echo "ERROR: --det, --run and --input are required."; exit 1; }
DET=$(echo "$DET" | tr '[:upper:]' '[:lower:]')

case "$DET" in
  hd) FCL=run_nf_elec_calib_hd.fcl ; CONVERT=convert_artroot_raw.C    ; CONV_FN=convert_artroot_raw ; MAGPREFIX=hraw-magnify ;;
  vd)
    if [[ "$FROM_HDF5" == "1" ]]; then FCL=run_nf_elec_calib_vd_hdf5.fcl
    else                               FCL=run_nf_elec_calib_vd.fcl ; fi
    CONVERT=vd_convert_artroot_raw.C ; CONV_FN=vd_convert_artroot_raw ; MAGPREFIX=hraw-pdvd-magnify ;;
  *) echo "ERROR: --det must be hd or vd"; exit 1 ;;
esac

mkdir -p "$ELEC/output"
cd "$ELEC/output"

# ---------------------------------------------------------------- env -------
# Common dunesw + WireCell environment, sourced inside the container per step.
read -r -d '' SETUP_ENV <<EOF || true
source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh
setup dunesw ${DUNESW_VER} -q ${DUNESW_QUAL}
export WIRECELL_PATH=${WCCFG}:${WCDATA}:\${WIRECELL_PATH}
export LD_LIBRARY_PATH=${WCLIB}:\${LD_LIBRARY_PATH}
export FHICL_FILE_PATH=${ELEC}:${DUNERECO_FCL}:\${FHICL_FILE_PATH}
EOF

run_in_container() {
  "$APPTAINER" exec \
    -B /cvmfs,/exp,/nashome,/opt,/etc/hostname,/etc/hosts,/etc/krb5.conf \
    --ipc --pid "$IMAGE" \
    /bin/bash -c "$1"
}

contains_step() { [[ ",$STEPS," == *",$1,"* ]]; }

# ---------------------------------------------------------------- 1. NF -----
if [[ "$FROM_HDF5" == "1" ]]; then NFOUT_PATTERN="*_eleccalib_h5.root"; else NFOUT_PATTERN="*_eleccalib.root"; fi
if contains_step nf; then
  echo "=========================================================="
  echo "[1/3] Noise filter + per-channel elec-response correction (${DET}, run ${RUN})"
  echo "=========================================================="
  run_in_container "${SETUP_ENV}
    cd ${ELEC}/output
    lar -n ${NEVTS} -c ${FCL} ${INPUT} 2>&1 | tee nf_eleccalib_${DET}_${RUN}.log"
fi

# locate the produced calib art-ROOT file
NFFILE=$(ls -t ${ELEC}/output/${NFOUT_PATTERN} 2>/dev/null | head -1 || true)

# ---------------------------------------------------------------- 2. convert
if contains_step convert; then
  echo "=========================================================="
  echo "[2/3] Convert corrected RawDigits -> magnify TH2 (h_raw)"
  echo "=========================================================="
  [[ -z "$NFFILE" ]] && { echo "ERROR: no calib art-ROOT (${NFOUT_PATTERN}) found for convert step."; exit 1; }
  echo "  input art-ROOT: $NFFILE"
  run_in_container "${SETUP_ENV}
    cd ${ELEC}/output
    root -b -q '${ELEC}/${CONVERT}(\"${NFFILE}\")' 2>&1 | tee convert_${DET}_${RUN}.log"
fi

# locate the magnify file (named hraw-...-RUN-EVT.root by the convert macro)
MAGFILE=$(ls -t ${ELEC}/output/${MAGPREFIX}-${RUN}-*.root 2>/dev/null | head -1 || true)
[[ -z "$MAGFILE" ]] && MAGFILE=$(ls -t ${ELEC}/output/${MAGPREFIX}-*.root 2>/dev/null | head -1 || true)

# ---------------------------------------------------------------- 3. fit ----
if contains_step fit; then
  echo "=========================================================="
  echo "[3/3] Fit each channel with the IDEAL cold-electronics response"
  echo "=========================================================="
  [[ -z "$MAGFILE" ]] && { echo "ERROR: no magnify file (${MAGPREFIX}-*.root) found for fit step."; exit 1; }
  echo "  input magnify: $MAGFILE"
  run_in_container "${SETUP_ENV}
    cd ${ELEC}/output
    root -b -q '${ELEC}/wf_fitter_ideal.C(\"${MAGFILE}\",\"${DET}\",${RUN})' 2>&1 | tee fit_${DET}_${RUN}.log"
  echo ""
  echo "Done.  Fit results:"
  echo "  ${ELEC}/output/txt_files/eleccalib_fit_${DET}_run_${RUN}.txt"
  echo "  ${ELEC}/output/root_files/eleccalib_fit_${DET}_run_${RUN}.root"
fi
