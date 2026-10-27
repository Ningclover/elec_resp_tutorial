# Environment setup for the electronics-response tutorial (for people and their AI agents)

**To an AI agent reading this:** your job is to give the user a shell in which every script of this tutorial runs, and in which a plain `cd` into the work folder switches the environment on (via direnv). Follow the steps in order and run the check in step 6 at the end. Do not copy absolute paths from the "reference environment" section into the user's files: they describe the author's machine. Ask the user before installing system packages (anything needing `sudo`).

---

## 1. What the tutorial needs

| need | used by | how it is provided in the reference setup |
|---|---|---|
| python ≥ 3.9 | every `script/*.py` | python 3.11.9 from the spack view, plus a private venv |
| numpy, matplotlib | all pages | pip, in the venv |
| scipy | page 4 fits (`least_squares`) | pip |
| h5py | pages 3, 4, 6, 10 (raw DAQ HDF5) | pip |
| uproot (+ awkward) | pages 7, 10 (read ROOT trees without ROOT) | pip |
| PDVD channel map `PD2VDTPCChannelMap_v2.txt` | pages 3, 4, 6, 10 | `/cvmfs/dune.opensciencegrid.org/products/dune/duneprototypes/<ver>/config_data/`, or set `PDVD_CHANMAP=<file>` |
| g++ | `check_formula_vs_wct.sh` only | system gcc 12 |
| ROOT on the host | **nothing required**: convenience only (PyROOT, quick `TBrowser`) | ROOT 6.32.02 from the spack view |
| apptainer + `/cvmfs` (dune, singularity.opensciencegrid.org) | pages 7, 9, 10: every LArSoft job, and the ROOT macros in `script/calib_chain/` | system apptainer; dunesw and the SL7 image come from `/cvmfs`, nothing to install |
| direnv | auto-activation on `cd` | Debian package `direnv`, hooked in `~/.bashrc` |
| the raw file `np02vd_raw_run045270_...hdf5` | pages 3, 4, 6, 10 input | DUNE data area (not shipped; ~1 GB) |

Everything LArSoft-related (dunesw, `lar`, the container's ROOT and gallery) comes from `/cvmfs` at run time. **Do not try to install LArSoft or dunesw locally.**

The machine-readable list of python packages is [`env/requirements.txt`](env/requirements.txt).

## 2. The reference environment (the author's machine, for comparison)

| item | value |
|---|---|
| OS | Debian GNU/Linux 12 (bookworm), x86_64 |
| shell | bash, with `eval "$(direnv hook bash)"` at the end of `~/.bashrc` |
| direnv | 2.32.1 |
| software stack | shared spack installation "wcwc" at `/wcwc` (Wire-Cell Workstation Cluster; a wrapper around spack), stack `wirecell` |
| spack view | `<workspace>/local`: every dependency of `wire-cell-toolkit` (hash `q3zolqz`, gcc 12.2.0) linked into one directory, the toolkit itself excluded (it is built from source in `<workspace>/toolkit`) |
| python | 3.11.9 from the view; venv `<workspace>/.direnv/python-3.11.9` made by direnv's `layout python` |
| python packages | numpy 2.3.5, scipy 1.16.3, matplotlib 3.10.8, h5py 3.15.1, uproot 5.7.5, awkward 2.10.0 |
| ROOT | 6.32.02 from the view (`root`, `root-config`, PyROOT via `local/lib/root`) |
| g++ | 12 (system) |
| apptainer | system `/usr/bin/apptainer` (`/cvmfs/oasis.opensciencegrid.org/mis/apptainer/current/bin/apptainer` also works) |
| LArSoft | dunesw v10_21_02d00 and v10_26_00d05 (`e26:prof`) from `/cvmfs`, inside `fnal-dev-sl7:latest` |

The `.envrc` of that workspace is a `wcwc`-generated file plus additions for Wire-Cell development. [`env/envrc.spack-view`](env/envrc.spack-view) is a cleaned-up copy that keeps only what the tutorial needs (the toolkit lines are commented out at the bottom).

## 3. Install and hook direnv (once per user)

direnv runs the file `.envrc` when you `cd` into a folder (or any folder below it) and undoes it when you leave. It only runs an `.envrc` you have approved with `direnv allow`, and asks again whenever the file changes.

```bash
command -v direnv || sudo apt install direnv        # Debian/Ubuntu; or: conda/brew/download the static binary
                                                    # from https://github.com/direnv/direnv/releases into ~/bin
echo 'eval "$(direnv hook bash)"' >> ~/.bashrc      # zsh: eval "$(direnv hook zsh)" in ~/.zshrc
exec bash                                           # reload the shell
```

Check: `type _direnv_hook` prints a function.

**Agents:** a non-interactive tool shell does not run the hook. Use `direnv exec <dir> <command>` or `eval "$(direnv export bash)"` in the folder, before each command.

## 4. Choose a software layout

Pick **one**. Recipe A is what the author uses; recipe C is enough for every tutorial script.

### Recipe A: shared wcwc/spack installation (same cluster as the author, `/wcwc` present)

A spack **view** is one directory of symlinks that makes a whole set of installed spack packages look like a single install prefix (`bin/`, `lib/`, `include/`, ...). Making it does not compile anything.

```bash
WS=/path/to/your/workspace            # e.g. /nfs/data/1/$USER/wirecell-working
mkdir -p $WS && cd $WS

wcwc find wire-cell-toolkit           # list the available builds; pick a gcc one (the author uses q3zolqz)
SPACK_DISABLE_LOCAL_CONFIG=1 SPACK_USER_CACHE_PATH=$WS/.spack-user-cache \
  wcwc view -S wirecell -s wire-cell-toolkit/q3zolqzd6wb2ofn27dkmife65e4i2wmf -e wire-cell-toolkit local
#    -S wirecell             the stack (package set) to use
#    -s SPEC                 the "seed": the view gets this package and all its dependencies
#    -e wire-cell-toolkit    leave the toolkit itself out (build it from source if you develop it)
#    local                   the view directory
ls local/bin/{python3,root,root-config}   # all three must exist (takes ~1 min, ~475 entries in local/bin)
```

- **Why the two variables:** spack also reads the user's own `~/.spack/` (its config, package repos, cache). If that directory was written by a *different* spack installation, `wcwc view` stops half-way with `Error: cannot unpack non-iterable NoneType object` and leaves an incomplete view (no ROOT). `SPACK_DISABLE_LOCAL_CONFIG=1` ignores `~/.spack/` and `SPACK_USER_CACHE_PATH` gives a fresh cache. Without such a `~/.spack/` the plain command works.
- `wcwc task wct-dev-view` does the same and also clones the toolkit and writes a development `.envrc`. Use it only when you also want to develop Wire-Cell (`wcwc task wct-dev-view --help`).

Then put the tutorial in the workspace and install the `.envrc`:

```bash
mkdir -p $WS/elec_response
cp -r /path/to/elec_resp_tutorial $WS/elec_response/      # or git clone / rsync
cp $WS/elec_response/elec_resp_tutorial/env/envrc.spack-view $WS/.envrc
cd $WS && direnv allow       # first entry makes .direnv/python-3.11.9 and pip-installs requirements.txt
```

### Recipe B: your own spack (no `/wcwc`)

Not tested by the author; it follows the same idea as recipe A.

```bash
spack install python@3.11 root@6.32 +python      # long: ROOT compiles for an hour or more
spack view symlink -i $WS/local python root      # or: spack view add -i ...  (older spack)
cp .../env/envrc.spack-view $WS/.envrc && cd $WS && direnv allow
```

If you do not need ROOT on the host (the tutorial does not), use recipe C instead.

### Recipe C: no spack, just python (enough for every tutorial script)

```bash
cd /path/to/elec_resp_tutorial
cp env/envrc.minimal .envrc
direnv allow          # first entry: makes .direnv/python-X.Y.Z with the system python3, pip-installs requirements.txt
```

This needs `python3` with the `venv` module (Debian: `sudo apt install python3-venv`) and internet access for pip. Tested from a clean shell on Debian 12 with the system python 3.11.2: install ~15 s, then `p1_ideal_response.py` runs.

## 5. What the `.envrc` files do, line by line

direnv provides these helper functions inside `.envrc`:

| line | effect |
|---|---|
| `load_prefix "$PWD/local"` | adds `local/bin` to `PATH`, `local/lib` to `LD_LIBRARY_PATH`, and the same for `PKG_CONFIG_PATH`, `CMAKE_PREFIX_PATH`, `MANPATH`, ... |
| `layout python python3` | makes (first time) and activates a venv in `.direnv/python-<version>` from whichever `python3` comes first on `PATH`; in recipe A that is the view's 3.11.9 |
| `path_add VAR dir` | **prepends** `dir` to `VAR`; with several `path_add` on one variable, the last one wins |
| `PATH_add dir` | prepends to `PATH` |
| `watch_file F` | re-runs the `.envrc` when `F` changes (here: re-install when `requirements.txt` is edited) |
| `export ROOTSYS="$(root-config --prefix)"` | ROOT's install prefix, taken from the view's `root-config` |
| `path_add PYTHONPATH local/lib/root` | makes `import ROOT` (PyROOT) work in the venv |
| `path_add PYTHONPATH $TUT/script` | lets scripts import `elecresp`, `pdvd_hdf5_decode` from anywhere |
| `PDVD_CHANMAP` (minimal only) | channel-map file when `/cvmfs` is missing |

Everything set by `.envrc` disappears when you `cd` out of the folder.

Rules worth keeping:

- Keep machine-specific paths in `.envrc` (or in driver shell scripts), not in the tutorial's python code.
- Do not point `PYTHONPATH` at another project's venv to borrow its packages. Install them into this venv with `requirements.txt`; otherwise this environment breaks silently when the other project changes.
- For Wire-Cell development in the same workspace, every `toolkit/build/<pkg>` directory with a `.so` must be in `LD_LIBRARY_PATH`. If one is missing, an older installed copy in `local/lib` is used, and new configuration keys are silently ignored.

## 6. Check

```bash
cd <folder with the .envrc>                 # direnv prints "direnv: loading .../.envrc"
./elec_response/elec_resp_tutorial/env/check_env.sh    # recipe A path; recipe C: ./env/check_env.sh
```

It prints `OK` / `MISSING` / `optional` for every item of section 1 and ends with `all required items found` (exit code 0). Then run the smoke tests:

```bash
cd script
python3 p1_ideal_response.py --outdir ../plots_test        # no input needed, ~5 s
python3 p2_pole_zero_tail.py ../data/protodunevd-params-channel-response-41603-no_avg_3.json.bz2 --outdir ../plots_test
# with the raw file:
python3 p3_raw_pulser.py /path/to/np02vd_raw_run045270_...hdf5 --outdir ../plots_test
# LArSoft (pages 9, 10): follow page 10 step by step; first `lar` job exit status 0 = OK
```

## 7. LArSoft: nothing to install

Pages 7, 9 and 10 run `lar` inside the Fermilab SL7 container. All you need on the host is `apptainer` and `/cvmfs` mounted with `dune.opensciencegrid.org`, `singularity.opensciencegrid.org` and `oasis.opensciencegrid.org`. [Page 10](10_simple_larsoft_rawdump.md) is the line-by-line first run; [page 9](09_running_larsoft.md) covers customizing fcl and jsonnet. Inside the container the host `.envrc` is **not** active (different OS, `--no-home`), and that is intended: the container gets its environment from `setup dunesw`.

| check | command |
|---|---|
| apptainer | `apptainer --version` |
| /cvmfs | `ls /cvmfs/dune.opensciencegrid.org/products/dune/dunesw/` |
| SL7 image | `ls -d /cvmfs/singularity.opensciencegrid.org/fermilab/fnal-dev-sl7:latest` |

## 8. Files

| file | purpose |
|---|---|
| [`env/requirements.txt`](env/requirements.txt) | python packages, with the tested versions |
| [`env/envrc.minimal`](env/envrc.minimal) | `.envrc` for recipe C (venv only) |
| [`env/envrc.spack-view`](env/envrc.spack-view) | `.envrc` for recipes A/B (spack view + venv), with optional Wire-Cell development lines |
| [`env/check_env.sh`](env/check_env.sh) | checks every requirement; exit code = number missing |
