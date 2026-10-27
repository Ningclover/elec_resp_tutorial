#!/bin/bash
# Make a writable FULL copy of this release's wire-cell-cfg and switch on the
# PDVD per-channel electronics-response correction in it (two one-line edits).
# Run INSIDE the container after "source setup_dunesw.sh".
#
# Usage:  ./make_cfg_copy.sh DEST [RESPONSE.json.bz2]
#   DEST     new directory, e.g. ./my-wire-cell-cfg
#   RESPONSE response file to copy into DEST (must have the SP tick: 500 ns)
# Then:   export WIRECELL_PATH=$PWD/DEST:$WIRECELL_PATH
set -euo pipefail
DEST=${1:?usage: $0 DEST [RESPONSE.json.bz2]}
RESP=${2:-}
[[ -n ${DUNERECO_DIR:-} ]] || { echo "setup dunesw first"; exit 1; }
[[ ! -e $DEST ]] || { echo "$DEST exists"; exit 1; }
cp -r "$DUNERECO_DIR/wire-cell-cfg" "$DEST"
chmod -R u+w "$DEST"
P=$DEST/pgrapher/experiment/protodunevd
NAME=protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2
[[ -n $RESP ]] && NAME=$(basename "$RESP") && cp "$RESP" "$DEST/"
[[ $(grep -c '^        chresp: null,$' "$P/params.jsonnet") == 1 ]] || { echo "params.jsonnet changed in this release: edit by hand"; exit 1; }
[[ $(grep -c '^      per_chan_resp: pc.name,$' "$P/sp.jsonnet") == 1 ]] || { echo "sp.jsonnet changed in this release: edit by hand"; exit 1; }
sed -i "s|^        chresp: null,\$|        chresp: \"$NAME\",|" "$P/params.jsonnet"
sed -i "s|^      per_chan_resp: pc.name,\$|      per_chan_resp: if anode.data.ident < 4 then pc.name else '',|" "$P/sp.jsonnet"
diff -r "$DUNERECO_DIR/wire-cell-cfg" "$DEST" || true
echo "now:  export WIRECELL_PATH=$(cd "$DEST" && pwd):\$WIRECELL_PATH"
