# Scripts of the electronics-response tutorial

See [../08_script_index.md](../08_script_index.md) for what each script does.

Quick start (from this directory):

```bash
./make_all_plots.sh /path/to/np02vd_raw_run045270_0000_df-s04-d0_dw_0_20260929T175649.hdf5 \
                    ../data/protodunevd-params-channel-response-41603-no_avg_3.json.bz2
```

- `p*.py`, `make_avg_pulses.py`, `pdvd_hdf5_decode.py`, `elecresp.py`: plain python3 (numpy, scipy, matplotlib, h5py).
- `calib_chain/`: LArSoft/ROOT calibration chain, copied from the earlier work; it still has the original FNAL paths.
- `sp_chain/`: self-contained NF+SP ON/OFF bundle (needs /cvmfs and apptainer).
