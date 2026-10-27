[Index](README.md) · [1 What is it](01_what_is_electronics_response.md) · [2 Tail](02_pole_zero_tail.md) · [3 Pulser data](03_calibration_data.md) · [4 Fitting](04_fitting_per_channel.md) · [5 Linearity](05_linearity_and_gain.md) · [6 In SP](06_correction_in_signal_processing.md) · [7 Validation](07_validation_and_running.md) · **8 Scripts** · [9 LArSoft how-to](09_running_larsoft.md) · [10 First LArSoft run](10_simple_larsoft_rawdump.md)

# 8. Script index

All scripts are in [`script/`](script/). The Python tutorial scripts run on any host with `python3` + `numpy`, `scipy`, `matplotlib`, `h5py` (and `/cvmfs` for the channel map). The LArSoft/ROOT chains run inside the SL7 container.

## Tutorial scripts (Python, written for this tutorial)

| script | page | input | output |
|---|---|---|---|
| [`make_all_plots.sh`](script/make_all_plots.sh) | all | pulser HDF5, response file, [gain-scan tables, toolkit source] | every `plots/p*.png`, `data/avg_pulses_run*.npz`, `data/fit_run*.npz` |
| [`elecresp.py`](script/elecresp.py) | 1, 2 | — (module) | `coldelec()`, `paramscoldelec()`, `transfer()`, `load_chresp()`; units |
| [`check_formula_vs_wct.sh`](script/check_formula_vs_wct.sh) | 1, 2 | toolkit source tree | compiles the C++ `coldelec`/`paramscoldelec` and compares them with `elecresp.py` |
| [`pdvd_hdf5_decode.py`](script/pdvd_hdf5_decode.py) | 3 | raw HDF5 | module + CLI: WIBEth decoding → (6144, nticks) ADC array by offline channel |
| [`p1_ideal_response.py`](script/p1_ideal_response.py) | 1 | — | `p1_ideal_*.png` |
| [`p2_pole_zero_tail.py`](script/p2_pole_zero_tail.py) | 2 | response file | `p2_toy_tails.png`, `p2_real_channels.png`, `p2_ratio_spectrum.png`, `p2_tail_fraction.png` |
| [`p3_raw_pulser.py`](script/p3_raw_pulser.py) | 3 | raw HDF5 | `p3_full_readout.png`, `p3_event_display.png`, `p3_one_pulse.png`, `p3_pulse_stack.png`, `p3_amplitude_map.png` |
| [`make_avg_pulses.py`](script/make_avg_pulses.py) | 4 | raw HDF5 | `avg_pulses_run<N>.npz`: average positive pulse per channel |
| [`p4_fit_pulses.py`](script/p4_fit_pulses.py) | 4 | average-pulse npz, [response file], [gain-scan table] | `fit_run<N>.npz` (3- and 7-parameter fits), `p4_*.png` |
| [`p5_linearity.py`](script/p5_linearity.py) | 5 | `gainscan_vd_ours_{daq,raw}.txt` | `p5_expected_line.png`, `p5_gain_map.png`, `p5_gain_dists.png` |
| [`p6_apply_correction.py`](script/p6_apply_correction.py) | 6 | raw HDF5, fit npz, [response file] | `p6_correction_spectrum[_chresp].png`, `p6_before_after[_chresp].png`, `p6_before_after_tp[_chresp].png` |

## Calibration chain, `script/calib_chain/` (LArSoft + ROOT, from earlier work)

These are copies of the scripts from the share bundle (`elec_res_share.tar.bz2`). **They contain the FNAL paths of the original area** (`/exp/dune/data/users/xning/proto-dune-vd/...`, `/exp/dune/data/users/ktellez/wf_correction/...`): edit the variables at the top before running them elsewhere. The calibration jsonnets they need (`nf_elec_calib.jsonnet`, `wcls_nf_elec_calib.jsonnet`) live in that area's `wire-cell-cfg/pgrapher/experiment/protodunevd/`.

| script | what it does |
|---|---|
| `run_elec_calib.sh` | outside-container driver: NF (perChannelShaper only) → convert → ideal fit. `--det vd\|hd --run N --input FILE [--from-hdf5] [--steps nf,convert,fit]` |
| `incontainer_elec_calib.sh` | the same, run from inside the container |
| `reprocess_vd_scan.sh` | all 9 VD scan runs → `output/scan/ourmag-pdvd-<run>-<evt>.root` (h_daq + h_raw from one job) |
| `run_gain_scan.sh` | runs the gain scans for vd/hd × daq/raw in one container session (older non-`_ours` variant) |
| `run_nf_elec_calib_vd_hdf5.fcl` | decode HDF5 + calibration NF, native 512 ns (`ClockSpeedTPC: 1.953125`) |
| `run_nf_elec_calib_vd.fcl`, `run_nf_elec_calib_hd.fcl` | the same from reco_stage1 art-ROOT (VD) / for HD |
| `vd_convert_orig_raw.C` | art-ROOT → magnify with **both** `h_daq` and `h_raw` (use this for before/after) |
| `vd_convert_artroot_raw.C`, `convert_artroot_raw.C`, `convert_artroot_hd.C` | art-ROOT → magnify `h_raw` (VD / HD) |
| `vd_orig_vs_raw_compare.C` | per-channel waveform overlay before/after, N random channels, multi-page PDF |
| `wf_fitter_VD_DECON.C` | K. Tellez Giron Flores' pulse finder + ideal fit (reference) |
| `wf_fitter_ideal.C` | the same fit, generalized for vd/hd, writes `txt_files/eleccalib_fit_<det>_run_<run>.txt` |
| `wf_gain_scan_ours.C` | per channel: ideal fit at each scan run, then height vs Q line → `gainscan_<det>_ours_<variant>.{root,txt}` |
| `plot_gain_tp_ours.C` | gain and tp histograms before/after |
| `plot_charge_vs_a0_ours.C` | height vs Q for chosen channels |
| `peaking_time_from_artroot.C` | single-run tp histogram from art-ROOT, with the response-file shaping overlaid |
| `get_pulser_dac.py` | reads `pulse_dac`, `gain`, `peak_time` per run from the public run-conditions database |

## NF+SP ON/OFF bundle, `script/sp_chain/` (self-contained)

A copy of `nfsp_share/`, with the 500 ns response file in `sp_chain/data/`. It has no machine-specific paths. Details are in [`../NFSP_PCRESP_NOTE.md`](../NFSP_PCRESP_NOTE.md).

| script | what it does |
|---|---|
| `run_all.sh INPUT.hdf5 [NEVTS] [RUNLABEL]` | four jobs {trad, dnn} × {off, on} in parallel, then dumps and plots |
| `run_nfsp.sh ARM PCRESP INPUT [NEVTS]` | one job; builds the ON configuration (full cfg copy + 2 edits) |
| `env.sh` | container entry + dunesw setup (`DUNESW_VER`, `SL7_IMAGE`, ... can be overridden) |
| `fcl/run_vd_nfsp_{trad,dnn}.fcl` | v10_26 job configs (decode + Wire-Cell NF/SP only) |
| `cfg_v10_26/pgrapher/experiment/protodunevd/{params,sp}.jsonnet` | the release files with the two one-line edits (manual setup) |
| `regen_chresp_tick.py IN --tick 500 [--check] [--fix-pole]` | re-grid a response file to another tick; find/fix k4 = k6 channels |
| `plot/dump_wires.{sh,C}`, `plot/plot_pcresp.py` | recob::Wire → flat TTree; ON/OFF plots and ratio tables |

## Minimal LArSoft example, `script/larsoft_example/` (page 9)

| script | what it does |
|---|---|
| `enter_sl7.sh [DIR ...]` | interactive shell in the SL7 container, with /cvmfs, the current dir and each DIR mounted |
| `setup_dunesw.sh [VER] [QUAL]` | `source` inside the container: UPS + dunesw setup (default v10_26_00d05 e26:prof) |
| `make_cfg_copy.sh DEST [RESPONSE]` | full copy of the release `wire-cell-cfg` + the two per-channel-response edits + the response file |
| `my_nfsp.fcl` | keepup job trimmed to decode + NF/SP, traditional ROI, resampler on |
| `my_nfsp_dnn_bottom.fcl` | keepup DNN-ROI job for a bottom-only file (`active_anodes: "0,1,2,3"`) |
| `run_example.sh INPUT RESPONSE [NEVTS] [WORKDIR]` | everything from the host: container → dunesw → cfg copy → `lar` → checks |

## Raw-waveform dump, `script/larsoft_rawdump/` (page 10)

| script | what it does |
|---|---|
| `dump_raw_pdvd.fcl` | `lar -c dump_raw_pdvd.fcl -n 1 -s RAW.hdf5`: HDF5 → PDVD TPC decoder → `<input>_rawdump.root` with only `tpcrawdecoder:daq` RawDigits |
| `rawdigit_to_tree.C(IN, OUT, [TAG])` | gallery: art-ROOT RawDigits → plain tree `raw` (run, event, channel, pedestal, adc[]); in the container |
| `plot_raw_waveforms.py TREE [--event N] [--channels U V W] [--outdir D]` | host python + uproot: `rawdump_{event_display,waveforms,pulse_zoom,baseline_noise}.png` |

## Data files, `data/`

| file | what |
|---|---|
| `avg_pulses_run45270.npz` | average positive pulse of every channel of run 45270 (from `make_avg_pulses.py`) |
| `fit_run45270.npz` | ideal (t0, A, tp) and 7-parameter (t0, A, tp, k3..k6) fit per channel, residual RMS |
| `gainscan_vd_ours_{daq,raw}.txt` | 9-run January scan, per-channel gain line before/after the correction (from `wf_gain_scan_ours.C`) |
| `protodunevd-params-channel-response-41603-no_avg_3.json.bz2` | the January response file, 512 ns grid |
| `protodunevd-params-channel-response-41603-no_avg_3-500ns.json.bz2` | the same re-gridded to 500 ns with the k4 = k6 channel fixed (used by SP) |

---
← [7. Validation](07_validation_and_running.md) · Next: [9. Running the LArSoft job →](09_running_larsoft.md)
