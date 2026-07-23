# JANA4ML4FPGA

EIC R&amp;D supported project developing ML on FPGA for streaming readout systems.
Built on [JANA2](https://github.com/JeffersonLab/JANA2); reads CODA/EVIO raw data
(FA125/FA250 ADC and SRS/GEM), reconstructs GEM clusters, and writes a flat ROOT
tree.

## Docker

The project ships a single image built from
[`docker/jana4ml4fpga/Dockerfile`](docker/jana4ml4fpga/Dockerfile): the full
software stack (ROOT, patched JANA2, jana4ml4fpga) on top of `eicdev/eic-base`.

Pull it:

```bash
docker pull eicdev/jana4ml4fpga:latest
```

…or build it locally (context is the repo root):

```bash
docker build -f docker/jana4ml4fpga/Dockerfile -t eicdev/jana4ml4fpga:latest .
```

Run an interactive shell:

```bash
docker run -it --rm eicdev/jana4ml4fpga:latest bash
```

- `-it` — interactive session (needed for a bash shell; without it `ctrl+c` may not work).
- `--rm` — remove the container's filesystem when it exits. Omit it to keep the
  container and restart it later with `docker start`.

Mount a host directory (e.g. your EVIO data) with `-v`:

```bash
docker run -it --rm -v /host/data:/mnt/data eicdev/jana4ml4fpga:latest
```

For C++ debugging (GDB) add:

```bash
--cap-add=SYS_PTRACE --security-opt seccomp=unconfined
```

Docs: [docker run](https://docs.docker.com/engine/reference/commandline/run/),
[bind mounts](https://docs.docker.com/storage/bind-mounts/).

## Install from source

`install_software.py` is a one-file installer: it bootstraps a self-contained
[Miniforge](https://github.com/conda-forge/miniforge) environment (ROOT, a C++20
toolchain, CMake) and then builds jana4ml4fpga. No pre-installed dependencies are
needed — JANA2 is fetched and built by the project itself.

```bash
wget https://raw.githubusercontent.com/JeffersonLab/jana4ml4fpga/main/install_software.py
python3 install_software.py
```

Everything is installed under the current directory (set `ML4FPGA_TOP_DIR` to
install elsewhere). When it finishes, load the environment and run:

```bash
source setup_env.sh          # bash; use setup_env.csh for (t)csh
jana4ml4fpga --help
```

Re-run a single step, or remove everything the installer created:

```bash
python3 install_software.py -s build_soft   # re-run only the build + install step
python3 install_software.py --clear         # remove miniforge, build, install, scripts
```

## Running jana4ml4fpga

Processing is driven by plugins. The active ones are:

| plugin        | role                                                          |
|---------------|--------------------------------------------------------------|
| `CDAQfile`    | serial EVIO file source                                      |
| `CDAQfileMT`  | mmap EVIO source with parallel deserialization (`-Pevio:parallel=1`) |
| `flat_tree`   | writes the events ROOT tree (per-thread + merged when `nthreads>1`) |
| `gemrecon2`   | GEM reconstruction (pedestals → clusters)                    |
| `root_output` | shared ROOT output file service                              |
| `log`         | logging service (loaded by default)                          |

### ADC readout → flat tree

```bash
jana4ml4fpga \
  -Pplugins=CDAQfile,flat_tree,root_output \
  -Pnthreads=1 \
  -Phistsfile=output.root \
  hd_rawdata_002633_000.evio
```

### SRS / GEM reconstruction

```bash
jana4ml4fpga \
  -Pplugins=CDAQfile,flat_tree,root_output,gemrecon2 \
  -Pdaq:srs_window_raw:ntsamples=3 \
  -Pgemrecon:mapping=scripts/db/2026_mapping_PS.cfg \
  -Phistsfile=output.root \
  hd_rawdata_008169_000.evio
```

### Parallel (multithreaded) SRS

Use the mmap source and worker threads. `flat_tree` automatically switches to
multithreaded output (per-thread trees merged via `TBufferMerger`); entries are
**unordered** — sort by the `event_number` leaf.

```bash
jana4ml4fpga \
  -Pplugins=CDAQfileMT,flat_tree,root_output,gemrecon2 \
  -Pevio:parallel=1 -Pnthreads=8 \
  -Pdaq:srs_window_raw:ntsamples=3 \
  -Pgemrecon:mapping=scripts/db/2026_mapping_PS.cfg \
  -Phistsfile=output.root \
  hd_rawdata_008169_000.evio
```

The multithreaded readout needs the allocator tuning
`GLIBC_TUNABLES=glibc.malloc.tcache_count=4096:glibc.malloc.arena_max=96`
(`setup_env.sh` sets it for you).

### Farm helper scripts

Under [`scripts/`](scripts): `run_evio.sh <run> <nevents> <mode> <srsbin> [file]`
runs a single serial job (`mode` = `ADC` / `DUMP` / `SRS`); `run_evio_fast.sh`
is the parallel (`CDAQfileMT`) SRS variant; `run_list*.sh` dispatch jobs across
farm nodes.

## Flags

### JANA

```sh
-Pnthreads=8                    # worker threads
-Pjana:nevents=10000            # events to process (0 = all)
-Pjana:nskip=10000              # events to skip
-Pjana:timeout=0                # disable the watchdog (needed when debugging/paused)
-Pjana:debug_plugin_loading=1   # print where plugins are loaded from
```

### Logging (aspect-based)

Each subsystem logs under an *aspect*: `evio` (readout), `gem` (reconstruction),
`out` (tree writer), `dqm`. Set the global default and override per aspect:

```sh
-Plog:level=info    # global default level
-Pevio:log=debug    # per-aspect override
-Pgem:log=info
-Pout:log=warn
```

### SRS / GEM

```sh
-Pdaq:srs_window_raw:ntsamples=3            # number of SRS time bins
-Pgemrecon:mapping=scripts/db/2026_mapping_PS.cfg
-Pgemrecon:plane_name_x=GEMTR1X             # clustering plane names (default URWELLX/Y)
-Pgemrecon:plane_name_y=GEMTR1Y
-Pgemrecon2:freeze_after=500                # freeze calibration after N events (0 = never)
```

### Parallel source / tree writer

```sh
-Pevio:parallel=1               # CDAQfileMT: parallel EVIO deserialization
-Pevio:prefetch_mb=256          # mmap read-ahead
-Pflat_tree:mt_output=events.root   # MT events file (default <histsfile>_events.root)
-Pflat_tree:flush_events=10000      # per-thread fills between TBufferMerger flushes
```

## TCP test sender / receiver

Two test executables built with the project:

```bash
# terminal 1
tcp_receiver

# terminal 2
tcp_sender -req=ex -cmd=send -host=localhost:20249
```

## Data

Raw EVIO data on the gluon farm:

```
/gluonraid3/data4/rawdata/trd/DATA/hd_rawdata_*.evio
```

Test setup (as recorded in the run logs):

- `rocFMWPC1` — TI master with a single FA250 board; last 3 channels are calorimeter data.
- `rocTRD1` — slave with 4 FA125 boards reading GEMTRD (bank 16) and SRS/GEM data (bank 17).

Selected physics runs:

```
===>  2 crates, 3 detectors: CAL/FA250, GEMTRD/FA125, GEM/SRS
Run_2531  GEMTRD:ok CAL:ok SRS:del=0x41 3bin 10APV  5.1M ev  *PHYS*
Run_2543  GEMTRD:ok CAL:ok SRS:del=0x41 3bin 10APV  1.1M ev  *PHYS*

===>  1 crate, 2 detectors: GEMTRD/FA125, GEM/SRS
Run_2548  GEMTRD:ok CAL:no SRS:del=0x40 9bin 10APV  1.5M ev  *PHYS*
Run_2567  GEMTRD:ok CAL:no SRS:del=0x40 9bin 10APV  3.2M ev  *PHYS*

===>  Mode8 (RAW) / Mode5 (short)
Run_2633  GEMTRD:ok CAL:on SRS:del=0x41 3bin 10APV  Mode8  250K ev
Run_2635  GEMTRD:ok CAL:on SRS:del=0x41 3bin 10APV  Mode5  250K ev
```
