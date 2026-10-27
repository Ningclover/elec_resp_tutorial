#!/bin/bash
# Check that this machine/shell has everything the electronics-response tutorial needs.
#   ./env/check_env.sh            (run from anywhere; exit code = number of REQUIRED items missing)
# Each line: OK / MISSING / optional, and which pages need the item.
HERE=$(cd "$(dirname "$0")/.." && pwd)
nmiss=0
ok()   { printf '  %-8s %-34s %s\n' "OK" "$1" "$2"; }
miss() { printf '  %-8s %-34s %s\n' "MISSING" "$1" "$2"; nmiss=$((nmiss+1)); }
opt()  { printf '  %-8s %-34s %s\n' "optional" "$1" "$2"; }

echo "== shell / direnv"
if command -v direnv >/dev/null; then ok "direnv $(direnv version)" "auto-activation on cd"; else opt "direnv" "not found: activate the env by hand"; fi
if [[ -n ${DIRENV_DIR:-} ]]; then ok "direnv active" "${DIRENV_DIR#-}"; else opt "direnv active" "no .envrc loaded in this shell"; fi

echo "== python (all pages)"
PY=$(command -v python3) || { miss "python3" "all pages"; PY=; }
if [[ -n $PY ]]; then
  ok "python3 $($PY -c 'import sys;print(sys.version.split()[0])')" "$PY"
  for m in numpy scipy matplotlib h5py uproot; do
    v=$($PY -c "import $m;print($m.__version__)" 2>/dev/null)
    case $m in uproot) need="pages 7, 10";; scipy) need="page 4";; h5py) need="pages 3, 4, 6";; *) need="all pages";; esac
    if [[ -n $v ]]; then ok "$m $v" "$need"; else miss "$m" "$need  (pip install -r env/requirements.txt)"; fi
  done
fi

echo "== data and detector files"
MAP=${PDVD_CHANMAP:-$(ls -d /cvmfs/dune.opensciencegrid.org/products/dune/duneprototypes/v10_2*/config_data/PD2VDTPCChannelMap_v2.txt 2>/dev/null | tail -1)}
if [[ -n $MAP && -r $MAP ]]; then ok "PDVD channel map" "$MAP"; else miss "PDVD channel map" "need /cvmfs/dune or PDVD_CHANMAP=<file>"; fi
for f in fit_run45270.npz protodunevd-params-channel-response-41603-no_avg_3.json.bz2 gainscan_vd_ours_daq.txt; do
  if [[ -r $HERE/data/$f ]]; then ok "data/$f" ""; else miss "data/$f" "shipped with the tutorial"; fi
done
H5=$(ls "$HERE"/../np02vd_raw_run045270_*.hdf5 2>/dev/null | head -1)
if [[ -n $H5 ]]; then ok "run 45270 raw HDF5" "$H5"; else opt "run 45270 raw HDF5" "pages 3, 4, 6, 10 input; get it from the DUNE data area"; fi

echo "== compilers / ROOT on the host"
if command -v g++ >/dev/null; then ok "g++ $(g++ -dumpversion)" "check_formula_vs_wct.sh"; else opt "g++" "only for check_formula_vs_wct.sh"; fi
if command -v root-config >/dev/null; then ok "ROOT $(root-config --version)" "$(root-config --prefix)"; else opt "ROOT (host)" "not needed: ROOT macros run in the SL7 container"; fi

echo "== LArSoft (pages 7, 9, 10)"
APP=$(command -v apptainer || ls /cvmfs/oasis.opensciencegrid.org/mis/apptainer/current/bin/apptainer 2>/dev/null)
if [[ -n $APP ]]; then ok "apptainer" "$APP"; else miss "apptainer" "pages 7, 9, 10"; fi
for d in /cvmfs/dune.opensciencegrid.org/products/dune/dunesw /cvmfs/singularity.opensciencegrid.org/fermilab/fnal-dev-sl7:latest; do
  if [[ -e $d ]]; then ok "$(basename "$d")" "$d"; else miss "$(basename "$d")" "/cvmfs not mounted? pages 7, 9, 10"; fi
done

echo
if (( nmiss == 0 )); then echo "all required items found"; else echo "$nmiss required item(s) missing"; fi
exit $nmiss
