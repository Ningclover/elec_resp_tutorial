#!/bin/bash
# Step 1: raw HDF5 -> artROOT with raw digits (tpcrawdecoder:daq) AND
# noise-filtered digits (wclsdatavdnf:raw). First event only.
# Run INSIDE the SL7 container:
#   ../launch_container.sh step1_nf/run_nf_pdvd.sh <raw.hdf5> [outdir]
INFILE=$(readlink -f "$1")
OUTDIR=$(readlink -f "${2:-.}")
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh
setup dunesw v10_21_02d00 -q e26:prof

# this folder holds wcls-nf-fixed.jsonnet; everything else comes from dunesw's wire-cell cfg
export WIRECELL_PATH=$HERE:$WIRECELL_PATH
export FHICL_FILE_PATH=$HERE:$FHICL_FILE_PATH

mkdir -p "$OUTDIR"; cd "$OUTDIR"
lar -n 1 -c wcls_nf_only_pdvd.fcl "$INFILE"
ls -la "$OUTDIR"/*_nf_stage1.root
