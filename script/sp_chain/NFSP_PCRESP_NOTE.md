> In this repository the bundle described below is [`script/sp_chain/`](./) (called `nfsp_share/` in the text). It does not include the v10_21 `cfg/` overlay of section 8, which the scripts do not use.

# ProtoDUNE-VD NF+SP with per-channel electronics response — run instructions

Companion to `ELEC_CALIB_NOTE.md`.  That note describes how the per-channel
cold-electronics response is **measured** (fit per channel).  This note
describes how to **use** the fitted response in the standard NF+SP chain, how
to compare it against the chain without it, and how to make the plots.

Everything needed is in the bundle `nfsp_share.tar.bz2` (section 8).  Two
ways to use it: the scripts (Quick start), or copying three files into
`wire-cell-cfg/` by hand (Manual setup).  Tested
with **dunesw v10_26_00d05** (WireCell 0.37.1), 2026-10-26.

Two ROI flavours ("arms") are run, each with the correction OFF and ON:

| arm  | fcl producer (`wclsdatavd` =)  | ROI                         |
|------|--------------------------------|-----------------------------|
| trad | `protodunevd_nfsp`             | traditional (threshold) ROI |
| dnn  | `protodunevd_nfsp_dnnroi`      | DNN-ROI + L1SP (hybrid)     |

## Quick start

```bash
tar xjf nfsp_share.tar.bz2
mkdir work && cd work
../nfsp_share/run_all.sh /path/to/np02vd_raw_run039252_1176_df-s03-d3_dw_0_20250830T054542.hdf5 3 039252
```

This runs the four jobs `{trad,dnn} x {off,on}` in parallel (about 4 min for
trad and 6.5 min for dnn on CPU, 3 events, peak memory 3–4 GB each), then
flattens the `gauss` output and makes the plots into `work/plots/`.

The only host requirements: `/cvmfs` (dune + larsoft + singularity
repositories) and apptainer.  The scripts enter the SL7 container themselves.
The plot step also needs a host `python3` with `numpy`, `uproot`,
`matplotlib`; if there is none, `run_all.sh` prints the command to run later.

## Manual setup: which file goes where in `wire-cell-cfg/`

If you would rather set things up by hand (or plug the correction into
your own fcl), it is three file copies into a copy of the release
`wire-cell-cfg`.  Inside the SL7 container:

```bash
source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh
setup dunesw v10_26_00d05 -q e26:prof

# 0. writable copy of this release's cfg (a full copy -- see section 2)
cp -r $DUNERECO_DIR/wire-cell-cfg  <your path>/wire-cell-cfg
chmod -R u+w <your path>/wire-cell-cfg
```

Then copy the bundle files:

| bundle file (`nfsp_share/...`) | copy to (`<your path>/wire-cell-cfg/...`) | what it does |
|---|---|---|
| `cfg_v10_26/pgrapher/experiment/protodunevd/params.jsonnet` | `pgrapher/experiment/protodunevd/params.jsonnet` | sets `chresp` to the response file — switches the correction ON |
| `cfg_v10_26/pgrapher/experiment/protodunevd/sp.jsonnet` | `pgrapher/experiment/protodunevd/sp.jsonnet` | applies it on bottom anodes 0–3 only |
| `data/protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2` | `protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2` (top level, next to the wire and field-response files) | the fitted per-channel response, 500 ns grid |

```bash
S=<path to>/nfsp_share
D=<your path>/wire-cell-cfg
cp $S/cfg_v10_26/pgrapher/experiment/protodunevd/params.jsonnet $D/pgrapher/experiment/protodunevd/
cp $S/cfg_v10_26/pgrapher/experiment/protodunevd/sp.jsonnet     $D/pgrapher/experiment/protodunevd/
cp $S/data/protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2 $D/

# put it first on the path, in every new shell, after "setup dunesw"
export WIRECELL_PATH=$D:$WIRECELL_PATH

# run with one of the bundle fcls (or your own -- needs use_resampler "true")
export FHICL_FILE_PATH=$S/fcl:$FHICL_FILE_PATH
lar -n 3 -c run_vd_nfsp_trad.fcl /path/to/file.hdf5     # or run_vd_nfsp_dnn.fcl
```

The two `cfg_v10_26` jsonnets are the **v10_26_00d05 release files with
exactly one edit each** (`diff` against `$DUNERECO_DIR/wire-cell-cfg` shows
only those lines), so they are only valid for that release.  For another
release, make the same two one-line edits in its own files instead (section 2
gives the exact lines).  To turn the correction OFF again, drop `$D` from
`WIRECELL_PATH`.

> **Do not copy the `cfg/` folder of the bundle onto v10_26.**  Those are
> the v10_21 files.  Their `sp.jsonnet` would work (same gate, plus a
> ctoffset split that defaults to the same 4 us), but their `params.jsonnet`
> refers to `protodunevd-wires-larsoft-v7-uvwfit.json.bz2`, which the
> release does not have, and also changes the geometry and drift speed; and
> their `wcls-nf-sp*.jsonnet` replace the newer v10_26 top-level configs
> (losing `active_anodes`, `use_l1sp_dnn`, ...).

Tested exactly as written above (1 event, trad): exit 0, correction applied
12 times on anode0–3 only, response file loaded from `wire-cell-cfg/`.

## 1. What the change actually is

The per-channel response is applied inside `OmnibusSigProc` via its
`per_chan_resp` setting.  Three facts drive the whole configuration:

1. **It is a SWAP, not a removal.**  The kernel is
   `spectrum *= E_ideal(f) / E_channel(f)` — it divides out the measured
   per-channel electronics response and installs the nominal one, which is
   what makes SP's deconvolution (always built from the nominal response)
   valid.  It is therefore **not idempotent**: applying it twice gives
   `(E_ideal/E_chan)^2`.  Apply it in **exactly one place** (SP only; NF is
   untouched).

2. **Bottom drifter only.**  The fitted file covers channels 0–6143 (anodes
   0–3).  `ParamsPerChannelResponse::channel_response()` **throws** on an
   unknown channel — there is no fallback to nominal.  So `sp.jsonnet`
   gates on the anode:

   ```jsonnet
   per_chan_resp: if anode.data.ident < 4 then pc.name else '',
   ```

3. **The response binning must equal the frame tick.**  `OmnibusSigProc`
   hard-throws on `cr_bins.binsize() != m_period`.  The fit was done at
   512 ns; the chain runs SP at 500 ns (`use_resampler: "true"` converts the
   bottom anodes), so the response file is re-gridded to 500 ns (section 7).

## 2. How ON / OFF is switched

Same fcl, same libraries for both; only the WireCell configuration differs,
selected by `WIRECELL_PATH` in `run_nfsp.sh`:

* **OFF** — `WIRECELL_PATH` is left as `setup dunesw` sets it: the release
  `wire-cell-cfg` exactly as shipped.  There `params.jsonnet` has
  `chresp: null`, so no per-channel response object is built and SP uses the
  nominal electronics response only.
* **ON** — `run_nfsp.sh` puts two directories in front:
  * `cfg_on_<dunesw version>/` — built automatically on first use as a
    **full copy** of `$DUNERECO_DIR/wire-cell-cfg` with exactly two edits in
    `pgrapher/experiment/protodunevd/`:

    | file | edit |
    |------|------|
    | `params.jsonnet` | `chresp: null,` → `chresp: "protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2",` |
    | `sp.jsonnet` | `per_chan_resp: pc.name,` → `per_chan_resp: if anode.data.ident < 4 then pc.name else '',` |

    The script checks each pattern occurs exactly once and stops if the
    release has changed.
  * `nfsp_share/data/` — holds the response file (referred to by bare
    filename).

  WireCell takes the first match on the path, so these win over the release.

> **Why a full copy and not just the two files?**  Release jsonnets import
> their siblings by relative path (`import 'params.jsonnet'`), and jsonnet
> resolves that against the importing file's own directory *before*
> `WIRECELL_PATH`.  An overlay holding only the two edited files is silently
> ignored.

> **Do not put a locally-built WireCell toolkit on `LD_LIBRARY_PATH`.**  A
> toolkit built on a modern host (glibc 2.34+) cannot load inside the SL7
> container (glibc 2.17); it fails as `GLIBC_2.32 not found`, then
> confusingly as a missing `libboost_program_options.so.1.85.0`.  The release
> WireCell already has everything needed — only the cfg is ours.

## 3. Running pieces by hand

```bash
# one job: ARM (trad|dnn), PCRESP (off|on), input, [nevts]
nfsp_share/run_nfsp.sh trad on /path/to/file.hdf5 3
#   -> runs/trad_on/*_nfsp_trad.root  and  runs/trad_on/nfsp.log

# flatten the gauss recob::Wires of one job to a plain TTree
nfsp_share/plot/dump_wires.sh runs/trad_on/<file>_nfsp_trad.root \
    wclsdatavd:gauss:pdvdnfsptrad plots/trad_on.root
#   (dnn arm: wclsdatavd:gauss:pdvdnfspdnn)

# plots, once plots/{trad,dnn}_{off,on}.root exist
python3 nfsp_share/plot/plot_pcresp.py plots --run 039252
```

Environment knobs (all optional):

| variable | default | meaning |
|----------|---------|---------|
| `OUTDIR` | `./runs` | where job directories go |
| `PLOTDIR` | `./plots` | (`run_all.sh`) dumps and PNGs |
| `ACTIVE_ANODES` | all | dnn arm: e.g. `"0,1,2,3"` for a bottom-only file |
| `DUNESW_VER` / `DUNESW_QUAL` | `v10_26_00d05` / `e26:prof` | release |
| `SL7_IMAGE` | cvmfs `fnal-dev-sl7:latest` | container image |
| `CONTAINER_HOME` | `$HOME` | home directory inside the container |
| `EXTRA_BINDS` | — | extra bind mounts, comma-separated |
| `PYTHON` | `python3` | (`run_all.sh`) python for the plot step |

**Each job gets its own directory** (`runs/<arm>_<on|off>`).  art's
TimeTracker/MemoryTracker write `time.db`/`mem.db` into the CWD; two jobs
sharing one directory corrupt each other and the second dies at
`RootOutput@EndJob` with `SQLExecutionError: unique_value expected`.

The fcls trim the path to `rns -> tpcrawdecoder -> triggerrawdecoder ->
wclsdatavd` (decode + WireCell NF/SP only, no imaging or pandora) and drop
RawDigits from the output.

## 4. Release-specific traps (v10_26)

* **In v10_26 `standard_reco_protodunevd_keepup.fcl` sets `wclsdatavd` to
  the DNN producer.**  An fcl that only trims the path (as the v10_21 bundle
  did) silently runs DNN-ROI as "trad".  `run_vd_nfsp_trad.fcl` therefore
  sets `@local::protodunevd_nfsp` explicitly **and** re-sets
  `use_resampler: "true"` (the stock `protodunevd_nfsp` has `"false"`, which
  leaves the bottom anodes at 512 ns and makes SP throw on the 500 ns
  response).
* **Bottom-only files** (e.g. `run039663 ... df-s04-d0`): the top anodes
  have 0 traces and the dnn arm dies in `L1SPFilterPD: unexpected input: 0
  ADC traces`.  Use `ACTIVE_ANODES="0,1,2,3"`.  The trad arm runs fine.
* The v10_21 bundle's own `wcls-nf-sp*.jsonnet` predate v10_26 (no
  `active_anodes`, `use_l1sp_dnn`, `use_dnnroi` extVars) — do not overlay them
  on a newer release.
* Electronics gain stays at the release default (`elecGain: 14`).

## 5. Check the correction actually fired

```bash
grep -c "applying ch-by-ch electronics response correction" runs/*/nfsp.log
grep -B1 "applying ch-by-ch" runs/*_on/nfsp.log | grep -o "anode[0-9]sigproc[0-9]" | sort -u
grep -ho "protodunevd-params-channel-response[^ \"]*" runs/*_on/nfsp.log | sort -u
```

Expect **`nevents x 4 anodes x 3 planes`** hits in each ON log (36 for 3
events), **0** in each OFF log, anodes **anode0..anode3 only**, and the file
`protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2`.
`run_nfsp.sh` prints the hit count when each job ends.

## 6. Reference results (run039252, first 3 events, dunesw v10_26_00d05)

| job      | exit | correction hits | wall  | peak RSS |
|----------|------|-----------------|-------|----------|
| trad off | 0    | 0               | 3:49  | 3.3 GB   |
| trad on  | 0    | 36 (anode0–3)   | 3:57  | 3.3 GB   |
| dnn off  | 0    | 0               | 6:15  | 4.1 GB   |
| dnn on   | 0    | 36 (anode0–3)   | 6:21  | 4.1 GB   |

Decon `gauss` charge, `sum|Q|` ON / OFF over the 3 events:

| anode | trad U | trad V | trad W | dnn U | dnn V | dnn W |
|-------|--------|--------|--------|-------|-------|-------|
| 0     | 0.996  | 1.041  | 1.039  | 1.051 | 1.027 | 1.039 |
| 1     | 1.081  | 0.988  | 1.041  | 1.065 | 1.043 | 1.041 |
| 2     | 1.076  | 1.058  | 1.054  | 1.063 | 1.050 | 1.054 |
| 3     | 1.066  | 1.054  | 1.058  | 1.055 | 1.053 | 1.058 |

Top anodes 4–7 are **bit-identical** ON vs OFF in both arms in this run.

> **The trad arm is not exactly reproducible run to run.**  Two trad jobs
> with *identical* config and input differ on 1–4 channels per event, a
> different channel each time, in one short ROI-sized stretch (seen: top
> channel 7994 by up to 247 e⁻; bottom channel 1346 by 54 e⁻ in two jobs
> started simultaneously; 4 bottom channels in ON vs ON).
> So the top-anode identity check in `summary.txt` can occasionally fail by
> a channel in the trad arm; that is this run-to-run variation, not the
> correction leaking to the top.  The dnn arm was exactly reproducible.  W agrees
between arms and with the earlier woodpecker frame-file measurement
(1.038/1.046/1.048/1.055).  The trad anode-1 V 0.988 is driven by one long,
nearly horizontal track (event 298567, channel ~1820) whose threshold ROI
loses charge when the response shape changes (that event alone: 0.924);
DNN-ROI does not show it.

### Plots (`plot_pcresp.py`)

* `gauss_<arm>_evt<E>_anode<A>.png` — one per arm × event × bottom anode.
  Rows U/V/W; columns OFF (before), ON (after), ON − OFF.  Colour style of
  woodpecker `plot-frames`: x = channel, y = tick, gauss on `hot_r` with a
  fixed 0–1000 e⁻ scale (white at 0); ON − OFF on `RdBu_r` centred at 0,
  ±10 × RMS of the non-zero samples.  `Σ|Q| ON/OFF` printed per plane.
* `summary_<arm>.png` — the ratio table plus OFF/ON waveforms of the
  highest-charge channel per plane (event 1, anode 1).
* `summary.txt` — the ratio tables and the top-anode identity check.

Two conversions the plot script does, worth knowing if you read the art
files yourself:

* **Units.**  `wclsFrameSaver` writes `recob::Wire = charge × 0.005`
  (`frame_scale`); the script divides by 0.005 to get electrons.
* **Channel → WCT anode.**  LArSoft numbers channels per CRP: 3072 each,
  U `[0,952)`, V `[952,1904)`, W `[1904,3072)`.  WCT anode `2k` is the
  **first half** of every plane of CRP `k` (U/V 476, W 584 channels), anode
  `2k+1` the second half — it is *not* `channel // 1536`.  (From
  `protodunevd-wires-larsoft-v5.json.bz2`.)

## 7. Response file: what it holds, re-gridding, and the pitfall that cost a day

### The `k4 == k6` pole

`Response::paramscoldelec` (`util/src/Response.cxx:359`) divides by
`(k4 - k6)`.  If a fit lands with **k4 exactly equal to k6**, that channel's
response is `NaN` for every sample.

That single NaN channel then destroys an **entire plane**: `decon_2D_init`
stacks the per-channel responses, FFTs them, and then runs a wire-direction
FFT (`OmnibusSigProc.cxx:1236`) that mixes every row.  The ROI logic rejects
everything and the plane reconstructs as **exactly zero charge** — with no
error message anywhere in the log.

Observed in `...41603-no_avg_3.json.bz2`, channel 3209
(`k4 = k6 = 0.1286484`), which silently wiped out anode 2 plane U.

Known degenerate channels in the PDVD response files:

| file | n | channels |
|------|---|----------|
| `...41603-no_avg_3` | 1 | 3209 |
| `...41573-no_avg_4` | 5 | 191, 3234, 3866, 3960, 4185 |
| `...41602-no_avg_3` | 7 | 284, 814, 3406, 4390, 4782, 5249, 6121 |

Several sit exactly on a fit bound (`k4 = 2.0000000`).  **Only exact equality
is fatal** — channels merely close to degenerate are numerically fine
(peaks within 3% of median even at `|k4-k6|/|k4| < 1e-6`).  The real fix
belongs in the fitting code.

### Re-gridding a response file to a different tick

`regen_chresp_tick.py` (in the bundle) does this, and checks for the pole on
every run:

```bash
# report only: grid, channel coverage, and any k4 == k6 channels
./regen_chresp_tick.py IN.json.bz2 --tick 500 --check

# re-grid 512 -> 500 ns AND break any k4 == k6 degeneracy
./regen_chresp_tick.py IN.json.bz2 --tick 500 --fix-pole
```

Re-gridding is **exact, not interpolated**: the file stores **no waveforms**,
only a 3-number header plus the fitted parameters per channel:

```
tick: 500.0   t0: -2750.0   nsamp: 103
channel_info: [ {channels:[0], gain:1.294e-12, shaping:2024.5,
                 k3:0.1713, k4:0.1279, k5:0.0862, k6:0.1279}, ... 6144 entries ]
```

At load time `ParamsPerChannelResponse::configure()` builds the time grid
`Binning(nsamp, t0, t0 + nsamp*tick)` from the header and evaluates the
analytic cold-electronics formula (`Response::ParamsColdElec`) on it.  The
grid lives in the file rather than in the job config, which is why a
separate file per tick is needed.  The complete difference between the
512 ns original and the shipped 500 ns file:

| field | 512 ns file | 500 ns file |
|-------|-------------|-------------|
| `tick` | 512 | 500 |
| `nsamp` | 100 | 103 (same ~51.5 us span) |
| `t0` | −2750 | −2750 (unchanged) |
| channel 3209 `k6` | 0.1286484 | 0.12864853 (`--fix-pole`) |
| other 6143 channels | — | identical |

`--fix-pole` scales `k6` by `(1 + 1e-6)`; the pole is removable so the
response is recovered to ~1e-5.

The shipped `...-500ns.json.bz2` was produced with exactly:

```bash
./regen_chresp_tick.py protodunevd-params-channel-response-41603-no_avg_3.json.bz2 \
    --tick 500 --fix-pole
```

## 8. Bundle contents (`nfsp_share.tar.bz2`)

```
nfsp_share/
  NFSP_PCRESP_NOTE.md         this note
  run_all.sh                  4 jobs + dumps + plots, one command
  run_nfsp.sh                 one job: ARM (trad|dnn) x PCRESP (off|on); builds cfg_on
  env.sh                      container entry + dunesw setup (sourced by the scripts)
  fcl/
      run_vd_nfsp_trad.fcl    traditional ROI (protodunevd_nfsp, resampler on)
      run_vd_nfsp_dnn.fcl     DNN-ROI + L1SP (keepup wclsdatavd)
      run_vd_nfsp_{trad,dnn}_hdf5.fcl   v10_21 versions (not used by the scripts;
                              on v10_26 the trad one runs DNN, see section 4)
  cfg_v10_26/pgrapher/experiment/protodunevd/
      params.jsonnet, sp.jsonnet
                              v10_26_00d05 release + one edit each; for the
                              manual setup (copy into wire-cell-cfg/)
  cfg/pgrapher/experiment/protodunevd/
      params.jsonnet, sp.jsonnet, wcls-nf-sp.jsonnet, wcls-nf-sp-dnnroi.jsonnet
                              v10_21 overlay (not used by the scripts).  Do not
                              put on WIRECELL_PATH with v10_26 (section 4).
                              params.jsonnet also carries non-pcresp edits:
                              wires v7-uvwfit, cpa_thick 60 mm, anode at the
                              shield plane, drift speed 1.568 mm/us.
  data/
      protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2
  plot/
      dump_wires.C            gallery macro: recob::Wire -> TTree (event, channel, view, signal)
      dump_wires.sh           runs it inside the container
      plot_pcresp.py          before/after plots + ratio table
  regen_chresp_tick.py        re-grid a response file to another tick + pole check
```

No outputs are included.  No machine-specific paths: everything is relative
to the bundle or given on the command line / via the variables in section 3.
