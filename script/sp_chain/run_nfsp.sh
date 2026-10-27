#!/bin/bash
#
# run_nfsp.sh ARM PCRESP INPUT.hdf5 [NEVTS]
#
#   ARM    : trad  (traditional threshold ROI, producer protodunevd_nfsp)
#            dnn   (DNN-ROI + L1SP, producer protodunevd_nfsp_dnnroi)
#   PCRESP : off   release wire-cell-cfg as shipped (chresp null)
#            on    per-channel electronics response, bottom anodes 0..3 only
#   NEVTS  : default 3
#
# Output: $OUTDIR/<ARM>_<PCRESP>/  (OUTDIR defaults to ./runs)
#         *_nfsp_<ARM>.root, nfsp.log
#
# Optional environment:
#   OUTDIR         output base directory (default ./runs)
#   ACTIVE_ANODES  dnn arm only, e.g. "0,1,2,3" for a file that holds only the
#                  bottom drifter (L1SPFilterPD throws on an empty anode)
#   DUNESW_VER / DUNESW_QUAL / SL7_IMAGE / CONTAINER_HOME / EXTRA_BINDS: see env.sh
#
# Runs inside an SL7 container (re-execs itself when started on the host).

set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
source "$HERE/env.sh"

if [[ $# -lt 3 ]]; then
  sed -n '3,22p' "$0"; exit 1
fi
ARM=$1; PC=$2; INPUT=$3; NEVTS=${4:-3}
[[ $ARM == trad || $ARM == dnn ]] || { echo "ARM must be trad|dnn"; exit 1; }
[[ $PC == on || $PC == off ]]     || { echo "PCRESP must be on|off"; exit 1; }
[[ -f $INPUT ]] || { echo "no such input: $INPUT"; exit 1; }
OUTDIR=${OUTDIR:-$PWD/runs}
mkdir -p "$OUTDIR"
OUTDIR=$(cd "$OUTDIR" && pwd)
INPUT=$(cd "$(dirname "$INPUT")" && pwd)/$(basename "$INPUT")

export OUTDIR
enter_container "$0" "$ARM" "$PC" "$INPUT" "$NEVTS" "$OUTDIR"
setup_dunesw

# ---- per-channel-response cfg ------------------------------------------------
# A FULL copy of this release's wire-cell-cfg with two edits.  Overlaying only
# the two edited files is NOT enough: release jsonnets import siblings by
# relative path (import 'params.jsonnet'), which bypasses WIRECELL_PATH order.
#   params.jsonnet : chresp null -> the 500 ns per-channel response file
#   sp.jsonnet     : per_chan_resp gated to anode ident < 4 (file covers
#                    channels 0..6143 only; unknown channel = KeyError)
build_cfg_on() {
  local dst=$1 src=$DUNERECO_DIR/wire-cell-cfg
  local tmp; tmp=$(mktemp -d "$dst.tmp.XXXX")
  cp -r "$src/." "$tmp/" && chmod -R u+w "$tmp"
  local p=$tmp/pgrapher/experiment/protodunevd
  [[ $(grep -c '^        chresp: null,$' "$p/params.jsonnet") == 1 ]] \
    || { echo "params.jsonnet: 'chresp: null,' not found once -- release changed, edit by hand"; return 1; }
  [[ $(grep -c '^      per_chan_resp: pc.name,$' "$p/sp.jsonnet") == 1 ]] \
    || { echo "sp.jsonnet: 'per_chan_resp: pc.name,' not found once -- release changed, edit by hand"; return 1; }
  sed -i 's|^        chresp: null,$|        chresp: "protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2",|' "$p/params.jsonnet"
  sed -i "s|^      per_chan_resp: pc.name,\$|      per_chan_resp: if anode.data.ident < 4 then pc.name else '',|" "$p/sp.jsonnet"
  # another job may have finished first; keep whichever landed
  mv -T "$tmp" "$dst" 2>/dev/null || rm -rf "$tmp"
}

if [[ $PC == on ]]; then
  CFG_ON=$OUTDIR/cfg_on_$DUNESW_VER
  [[ -d $CFG_ON ]] || build_cfg_on "$CFG_ON" || exit 1
  export WIRECELL_PATH=$CFG_ON:$HERE/data:${WIRECELL_PATH:-}
fi

# ---- fcl -----------------------------------------------------------------------
RUN=$OUTDIR/${ARM}_${PC}
mkdir -p "$RUN"; cd "$RUN" || exit 1
FCL=run_vd_nfsp_${ARM}.fcl
if [[ $ARM == dnn && -n ${ACTIVE_ANODES:-} ]]; then
  printf '#include "%s"\nphysics.producers.wclsdatavd.wcls_main.params.active_anodes: "%s"\n' \
    "$FCL" "$ACTIVE_ANODES" > job.fcl
  FCL=job.fcl
fi
export FHICL_FILE_PATH=$RUN:$HERE/fcl:${FHICL_FILE_PATH:-}

{
  echo "dunesw $DUNESW_VER -q $DUNESW_QUAL  arm=$ARM pcresp=$PC  nevts=$NEVTS"
  echo "input $INPUT"
  echo "WIRECELL_PATH=$WIRECELL_PATH"
} > nfsp.log
/usr/bin/time -v lar -n "$NEVTS" -c "$FCL" "$INPUT" >> nfsp.log 2>&1
rc=$?
echo "exit code: $rc" >> nfsp.log
echo "$ARM/$PC: exit $rc, correction applied $(grep -c 'applying ch-by-ch electronics response correction' nfsp.log) times  ($RUN)"
exit $rc
