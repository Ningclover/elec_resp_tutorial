#!/bin/bash
# Check that elecresp.py reproduces the toolkit's C++ response functions.
#
# Cuts Response::coldelec and Response::paramscoldelec out of the toolkit
# source, compiles them stand-alone with g++, evaluates them on a time grid for
# a few parameter sets, and compares with elecresp.py.
#
# Usage:  ./check_formula_vs_wct.sh <path-to-wire-cell-toolkit>
set -euo pipefail
WCT=${1:?usage: $0 <path-to-wire-cell-toolkit>}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$WCT/util/src/Response.cxx
WORK=$(mktemp -d "${TMPDIR:-/tmp}/elecresp_check.XXXX")

{
  echo '#include <cmath>'
  echo '#include <cstdio>'
  echo 'using namespace std;'
  echo 'namespace units { const double microsecond = 1000.0; }   // WCT base time unit is ns'
  echo 'struct Response { static double coldelec(double,double,double);'
  echo '  static double paramscoldelec(double,double,double,double,double,double,double); };'
  # the two function bodies, verbatim
  awk '/^double Response::coldelec\(/,/^}/' "$SRC"
  awk '/^double Response::paramscoldelec\(/,/^}/' "$SRC"
  cat <<'EOF'
int main() {
  // gain in mV/fC (any unit works: both are linear in gain), shaping in ns
  // set 3 has k4 == k6 exactly: the formula divides by (k4-k6) -> NaN
  double P[4][6] = {{2200, 0.1713, 0.1279, 0.0862, 0.1286, 0},
                    {2050, 0.45, 0.41, 0.029, 0.034, 0},
                    {2000, 0.10, 0.10, 0.05, 0.05, 0},
                    {2025, 0.1713, 0.1286484, 0.0862, 0.1286484, 0}};
  for (int k = 0; k < 4; ++k)
    for (int i = 1; i < 120; ++i) {
      double t = i * 250.0;
      printf("%d %.17g %.17g %.17g\n", k, t,
             Response::coldelec(t, 7.8, P[k][0]),
             Response::paramscoldelec(t, 7.8, P[k][0], P[k][1], P[k][2], P[k][3], P[k][4]));
    }
}
EOF
} > "$WORK/check.cxx"

g++ -O2 -o "$WORK/check" "$WORK/check.cxx"
"$WORK/check" > "$WORK/cpp.txt"

python3 - "$WORK/cpp.txt" "$HERE" <<'EOF'
import sys
import numpy as np
sys.path.insert(0, sys.argv[2])
from elecresp import coldelec, paramscoldelec
d = np.loadtxt(sys.argv[1])
P = [(2.2, 0.1713, 0.1279, 0.0862, 0.1286), (2.05, 0.45, 0.41, 0.029, 0.034), (2.0, 0.10, 0.10, 0.05, 0.05),
     (2.025, 0.1713, 0.1286484, 0.0862, 0.1286484)]
np.seterr(all="ignore")
for k, (tp, k3, k4, k5, k6) in enumerate(P):
    m = d[:, 0] == k
    t = d[m, 1] / 1000.0
    c_py = coldelec(t, 7.8, tp)
    p_py = paramscoldelec(t, 7.8, tp, k3, k4, k5, k6)
    dc = np.max(np.abs(c_py - d[m, 2])) / np.max(np.abs(d[m, 2]))
    if np.isnan(d[m, 3]).all():
        print(f"set {k}: k4 == k6 = {k4}: C++ gives NaN at every sample, python NaN too: {np.isnan(p_py).all()}")
        continue
    dp = np.max(np.abs(p_py - d[m, 3])) / np.max(np.abs(d[m, 3]))
    print(f"set {k}: tp={tp} k=({k3},{k4},{k5},{k6})  max rel diff  coldelec {dc:.1e}   paramscoldelec {dp:.1e}")
EOF
rm -rf "$WORK"
