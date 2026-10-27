#!/usr/bin/env python3
"""get_pulser_dac.py -- pull the injected-charge pulser DAC (and gain, peak_time,
pulser flag) per run from the DUNE conditions DB (condb), for the PDHD/PDVD
electronics-calibration pulser runs.

condb is a PUBLIC read endpoint (no token needed), the same source the dunecat
web app uses for its "RUN CONDITIONS (CONDB)" panel.

  endpoint : https://dbdata0vm.fnal.gov:9443/dune_runcon_prod
  query    : /search?folder=<folder>&format=json&t=<run>
             -> returns rows up to tv=<run>; take the row with tv==run, channel==0
  folders  : HD  pdunesp.run_conditionstest
             VD  pdunesp.run_conditions_vd
  field    : pulse_dac  (also gain mV/fC, peak_time us, pulser bool)

Usage:  python3 get_pulser_dac.py
Writes: pulser_dac_table.txt  (det run pulse_dac pulser gain peak_time)
"""
import json
import sys
import urllib.parse
import urllib.request

BASE = "https://dbdata0vm.fnal.gov:9443/dune_runcon_prod"
FOLDER = {"hd": "pdunesp.run_conditionstest", "vd": "pdunesp.run_conditions_vd"}

# Calibration pulser runs we have HDF5 / magnify for.
RUNS = {
    "vd": [41573, 41574, 41577, 41582, 41587, 41593, 41597, 41602, 41603],
    "hd": [30367, 30376, 30385, 30394, 30404, 30413, 30421, 30430],
}

FIELDS = ("pulse_dac", "pulser", "gain", "peak_time", "ac_couple", "cold")


def fetch_run(folder, run):
    params = urllib.parse.urlencode([("folder", folder), ("format", "json"), ("t", str(run))])
    url = f"{BASE}/search?{params}"
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        payload = json.loads(resp.read().decode("utf-8"))
    for row in payload.get("rows", []):
        tv = row.get("tv")
        if tv is None:
            continue
        if int(tv) == run and row.get("channel") in (0, None):
            return row
    return None


def main():
    out_path = "pulser_dac_table.txt"
    lines = ["# det run pulse_dac pulser gain peak_time ac_couple cold"]
    print(f"{'det':>3} {'run':>6} {'pulse_dac':>9} {'pulser':>6} {'gain':>5} {'peak':>4}")
    for det in ("hd", "vd"):
        folder = FOLDER[det]
        for run in RUNS[det]:
            try:
                row = fetch_run(folder, run)
            except Exception as e:
                print(f"{det:>3} {run:>6}  ERROR: {e}")
                lines.append(f"{det} {run} ERROR {e}")
                continue
            if row is None:
                print(f"{det:>3} {run:>6}  (not found in {folder})")
                lines.append(f"{det} {run} NOT_FOUND")
                continue
            vals = {k: row.get(k) for k in FIELDS}
            print(f"{det:>3} {run:>6} {str(vals['pulse_dac']):>9} "
                  f"{str(vals['pulser']):>6} {str(vals['gain']):>5} {str(vals['peak_time']):>4}")
            lines.append(f"{det} {run} {vals['pulse_dac']} {vals['pulser']} "
                         f"{vals['gain']} {vals['peak_time']} {vals['ac_couple']} {vals['cold']}")
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"\nwrote {out_path}")


if __name__ == "__main__":
    main()
