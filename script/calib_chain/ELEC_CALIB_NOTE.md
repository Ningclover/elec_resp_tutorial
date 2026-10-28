> Instructions written for the FNAL area: the `/exp/dune/...` paths exist only on FNAL machines. The scripts they name are copied in this folder.

# Electronics Response Calibration — ProtoDUNE-VD


## 1. Environment

Set up dunesw, please use this version. it is the most recent one. 

```bash
source /cvmfs/dune.opensciencegrid.org/products/dune/setup_dune.sh
setup dunesw v10_21_02d00 -q e26:prof
```

## 2. One-time setup: WireCell configuration

Copy the wire-cell-cfg shipped with this dunereco version:

```bash
cp -r /cvmfs/dune.opensciencegrid.org/products/dune/dunereco/v10_21_02d00/wire-cell-cfg/ <your path>/wire-cell-cfg/
```

Add the two calibration jsonnets on top of it:

```bash
cp /exp/dune/data/users/xning/proto-dune-vd/wire-cell-cfg/pgrapher/experiment/protodunevd/nf_elec_calib.jsonnet \
   /exp/dune/data/users/xning/proto-dune-vd/wire-cell-cfg/pgrapher/experiment/protodunevd/wcls_nf_elec_calib.jsonnet \
   <your path>/wire-cell-cfg/pgrapher/experiment/protodunevd/
```

Point WireCell at your copy. Do this in every new shell, **after** `setup dunesw`:

```bash
export WIRECELL_PATH=<your path>/wire-cell-cfg:$WIRECELL_PATH
```

## 3. Working directory and fcl

```bash
mkdir -p <your path>/elec_res/output
cd <your path>/elec_res/output
cp /exp/dune/data/users/xning/proto-dune-vd/elec_res/run_nf_elec_calib_vd_hdf5.fcl .
```

## 4.  NF with perChannelShaper (from raw HDF5)

```bash
lar -n 1 -c run_nf_elec_calib_vd_hdf5.fcl \
    /exp/dune/data/users/ktellez/wf_correction/np02vd_raw_run041602_0000_df-s04-d1_dw_0_20260107T233130.hdf5
```

- Output: `*_eleccalib_h5.root` in the current directory.
- To start from an art-ROOT file (already-decoded RawDigits) instead, use
  `/exp/dune/data/users/xning/proto-dune-vd/elec_res/run_nf_elec_calib_vd.fcl`
  (output is then `*_eleccalib.root`).


I believe for the later two steps, you have your own tools to do it.

## 5. Convert corrected RawDigits → magnify TH2 (`h_raw`)

Pass the file produced in step 4 as the argument:

```bash
root -b -q '/exp/dune/data/users/xning/proto-dune-vd/elec_res/vd_convert_artroot_raw.C("np02vd_raw_run041602_0000_..._eleccalib_h5.root")'
```

- Output: `hraw-pdvd-magnify-<run>-<event>.root`

## 6.  Fit each channel with the ideal cold-electronics response

Arguments: magnify file, detector (`"vd"`), run number.

```bash
root -b -q '/exp/dune/data/users/xning/proto-dune-vd/elec_res/wf_fitter_ideal.C("hraw-pdvd-magnify-41602-0.root","vd",41602)'
```

Results:

```
output/txt_files/eleccalib_fit_vd_run_41602.txt
output/root_files/eleccalib_fit_vd_run_41602.root
```
