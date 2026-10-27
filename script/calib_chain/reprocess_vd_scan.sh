#!/bin/bash
# reprocess_vd_scan.sh -- reprocess ALL 9 PDVD pulser-scan runs from source
# (no Karla magnify files), using a SINGLE chresp (41603 no_avg_3) for every run
# since the cold-electronics response is run-independent across the charge scan.
#
# Per run: WireCell calib NF (perChannelShaper) -> our magnify (h_daq + h_raw).
#   41602 : from raw HDF5            (run_nf_elec_calib_vd_hdf5.fcl)
#   others: from reco_stage1 artROOT (run_nf_elec_calib_vd.fcl)
#
# Run OUTSIDE the container; wraps everything in one apptainer exec (one UPS setup).

set -uo pipefail
PROTO=/exp/dune/data/users/xning/proto-dune-vd
ELEC=$PROTO/elec_res
WCCFG=$PROTO/wire-cell-cfg
WCDATA=$PROTO/git/wire-cell-data
WCLIB=$PROTO/wire-cell-toolkit/install/lib
DUNERECO_FCL=$PROTO/git/dunereco/dunereco/DUNEWireCell
KARLA=/exp/dune/data/users/ktellez/wf_correction
OUT=$ELEC/output/scan
DUNESW_VER=v10_20_08d00
DUNESW_QUAL=e26:prof
APPTAINER=/cvmfs/oasis.opensciencegrid.org/mis/apptainer/current/bin/apptainer
IMAGE=/cvmfs/singularity.opensciencegrid.org/fermilab/fnal-dev-sl7:latest

mkdir -p "$OUT"

# run -> input file ; 41602 flagged hdf5
declare -A INPUT FROMH5
INPUT[41573]=$KARLA/np02vd_raw_run041573_0000_df-s04-d0_dw_0_20260107T223634_reco_stage1_20260510T040555_keepup.root
INPUT[41574]=$KARLA/np02vd_raw_run041574_0000_df-s04-d0_dw_0_20260107T223824_reco_stage1_20260510T022418_keepup.root
INPUT[41577]=$KARLA/np02vd_raw_run041577_0000_df-s04-d0_dw_0_20260107T224352_reco_stage1_20260510T021824_keepup.root
INPUT[41582]=$KARLA/np02vd_raw_run041582_0000_df-s04-d0_dw_0_20260107T225306_reco_stage1_20260510T021539_keepup.root
INPUT[41587]=$KARLA/np02vd_raw_run041587_0000_df-s04-d0_dw_0_20260107T230207_reco_stage1_20260510T021304_keepup.root
INPUT[41593]=$KARLA/np02vd_raw_run041593_0000_df-s04-d0_dw_0_20260107T231404_reco_stage1_20260510T020720_keepup.root
INPUT[41597]=$KARLA/np02vd_raw_run041597_0000_df-s04-d0_dw_0_20260107T232217_reco_stage1_20260510T015910_keepup.root
INPUT[41602]=$KARLA/np02vd_raw_run041602_0000_df-s04-d1_dw_0_20260107T233130.hdf5
INPUT[41603]=$KARLA/np02vd_raw_run041603_0000_df-s04-d0_dw_0_20260107T233322_reco_stage1_20260513T194210_keepup.root
FROMH5[41602]=1

RUNS=(41573 41574 41577 41582 41587 41593 41597 41602 41603)

read -r -d '' BODY <<'OUTER' || true
source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh
setup dunesw __VER__ -q __QUAL__
export WIRECELL_PATH=__WCCFG__:__WCDATA__:${WIRECELL_PATH:-}
export LD_LIBRARY_PATH=__WCLIB__:${LD_LIBRARY_PATH:-}
export FHICL_FILE_PATH=__ELEC__:__DUNERECO__:${FHICL_FILE_PATH:-}
cd __OUT__
OUTER
BODY=${BODY//__VER__/$DUNESW_VER}
BODY=${BODY//__QUAL__/$DUNESW_QUAL}
BODY=${BODY//__WCCFG__/$WCCFG}
BODY=${BODY//__WCDATA__/$WCDATA}
BODY=${BODY//__WCLIB__/$WCLIB}
BODY=${BODY//__ELEC__/$ELEC}
BODY=${BODY//__DUNERECO__/$DUNERECO_FCL}
BODY=${BODY//__OUT__/$OUT}

for r in "${RUNS[@]}"; do
  inp=${INPUT[$r]}
  if [[ "${FROMH5[$r]:-0}" == "1" ]]; then fcl=run_nf_elec_calib_vd_hdf5.fcl; glob='*_eleccalib_h5.root'
  else                                     fcl=run_nf_elec_calib_vd.fcl;      glob='*_eleccalib.root'; fi
  BODY+="
echo '==================== run $r ===================='
lar -n 1 -c $fcl '$inp' 2>&1 | tee nf_${r}.log
nffile=\$(ls -t $OUT/$glob 2>/dev/null | head -1)
echo \"  NF out: \$nffile\"
[[ -z \"\$nffile\" ]] && { echo 'ERROR: no NF output for $r'; } || \
  root -b -q \"$ELEC/vd_convert_orig_raw.C(\\\"\$nffile\\\")\" 2>&1 | tee conv_${r}.log
# rename the eleccalib art-ROOT so the next run's glob doesn't pick it up
[[ -n \"\$nffile\" ]] && mv -f \"\$nffile\" \"\${nffile}.run${r}.done\"
"
done

echo "launching 9-run reprocess inside container -> $OUT"
"$APPTAINER" exec -B /cvmfs,/exp,/nashome,/opt,/etc/hostname,/etc/hosts,/etc/krb5.conf \
  --ipc --pid "$IMAGE" /bin/bash -c "$BODY"
echo "DONE. magnify files:"
ls -la "$OUT"/ourmag-pdvd-*.root 2>/dev/null
