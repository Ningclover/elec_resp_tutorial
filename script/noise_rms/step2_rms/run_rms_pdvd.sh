#!/bin/bash
# Step 2: NF artROOT -> per-channel noise RMS -> ENC histograms (raw vs filtered).
# Run INSIDE the SL7 container:
#   ../launch_container.sh step2_rms/run_rms_pdvd.sh <..._nf_stage1.root> [outdir]
# Output: <outdir>/run_<run>/evt_<evt>/rms_*.png  (noise histograms)
INFILE=$(readlink -f "$1")
OUTDIR=$(readlink -f "${2:-./NF_plots}")
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh
setup dunesw v10_21_02d00 -q e26:prof

export PLOT_STYLE_DIR=$HERE      # where rootlogon.C.c (plot style) lives
cd "$HERE/macro"
root -l -b -q -e ".L convert_artroot_noise_plot.C" \
     -e "convert_artroot_DQM(\"${INFILE}\", \"${OUTDIR}\")"
ls "$OUTDIR"/run_*/evt_*/rms_*.png
