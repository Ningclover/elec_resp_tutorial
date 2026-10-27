#!/bin/bash
# dump_wires.sh IN.root TAG OUT.root
# Flatten one recob::Wire product to a plain TTree (runs dump_wires.C with
# gallery inside SL7 + dunesw).  TAG e.g. wclsdatavd:gauss:pdvdnfsptrad
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
source "$HERE/../env.sh"
[[ $# -eq 3 ]] || { sed -n '2,4p' "$0"; exit 1; }
IN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
OUT=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
enter_container "$0" "$IN" "$2" "$OUT"
setup_dunesw
# compile into a scratch dir so the bundle can stay read-only
BUILD=$(mktemp -d)
cp "$HERE/dump_wires.C" "$BUILD/" && cd "$BUILD" || exit 1
root -l -b -q "dump_wires.C+(\"$IN\",\"$2\",\"$OUT\")"
rc=$?
rm -rf "$BUILD"
exit $rc
