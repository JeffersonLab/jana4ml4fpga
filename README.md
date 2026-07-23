# JANA4ML4FPGA

EIC R&amp;D supported project developing ML on FPGA for streaming readout systems

## Install from source

`install_software.py` is a one-file installer: it bootstraps a self-contained
[Miniforge](https://github.com/conda-forge/miniforge) environment (ROOT, a C++20
toolchain, CMake) and then builds jana4ml4fpga. No pre-installed dependencies are
needed - JANA2 is fetched and built by the project itself.

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

## How to run

### Docker images

There are two images:

- [**eicdev/ml4fpga-pre**](https://hub.docker.com/r/eicdev/ml4fpga-pre) - with all dependencies but without JANA4ML4FPGA built.
  This image is used for CI or development reasons and is not designed for running JANA4ML4FPGA   
- [**eicdev/ml4fpga**](https://hub.docker.com/r/eicdev/ml4fpga) - The image can be used to run JANA4ML4FPGA

## Running in docker
*(extended information about options of running this docker containers)*

To download/update the container

```bash
docker pull eicdev/ml4fpga:latest
```

Running docker:

```bash
docker run -it --rm eicdev/ml4fpga:latest bash
```

- ```--rm``` flag Docker **automatically cleans up the container** and 
  remove the file system **when the container exits**.
- ```-it``` flag enables interactive session. Without this flag `ctrl+c` may not work. 
  In general `-it` is used to run e.g. bash session (see below)
- 

**Extremely shallow docker crash course** 

Docker utilizes the pull command to download a package called an **image**. 
When Docker executes the image, it runs within a **container**. 
Each time the `docker run` command is called, a new container is generated.
Docker creates a modifiable layer over the **image** and starts the **container** with the given command.

If the `--rm` flag is not used, stopping the container does not remove it.
A stopped container can be restarted with all its previous changes intact using the docker start command.

Docker functions similarly to tmux or screen, allowing you to reconnect to the running image, 
attach multiple bash shells, and even reconnect if the container is stopped. 
This feature facilitates easier debugging and data retention. 
However, be cautious when using the --rm flag, as it removes the container upon stopping.


Docker documentation:
[docker run](https://docs.docker.com/engine/reference/commandline/run/),
[docker start](https://docs.docker.com/engine/reference/commandline/start/),
[--rm flag](https://docs.docker.com/engine/reference/run/#clean-up---rm).


### Add your system directory inside docker

You can bind any directory on your system to docker image by using **-v** flag:

```bash 
-v <your/directory>:<docker/directory>

# /mnt is a good place to mount directories inside a container
docker run -it --rm -v /host/dir:/mnt/data eicdev/ml4fpga:latest
```
More information on [docker bind](https://docs.docker.com/storage/bind-mounts/),
There are other mechanisms of how to manage data in docker. 
[the official documentation on managing data in docker](https://docs.docker.com/storage/)


**Debugging** : To do C++ debugging (run GDB or so) one has to specify additional flags
(the debugging is switched off by docker by default for security reasons):

```--cap-add=SYS_PTRACE --security-opt seccomp=unconfined```



## Data

All EVIO DATA is here:

```
/gluonraid3/data4/rawdata/trd/DATA/hd_rawdata_*.evio
```

Physics runs:
-------------

```
===>  2 crates , 3 detectors : CAL/FA250, GEMTRD/FA125 , GEM/SRS
CODA::  Run_2531 GEMTRD:ok; CAL:ok; SRS:del=0x41; ROSYBC=0x90 3bin; 10APV;OK; TRDpos=100; 5.1M ev  *PHYS* !!!!
cDAQ::  Run_2543 GEMTRD:ok; CAL:ok; SRS:del=0x41; ROSYBC=0x90 3bin; 10APV;OK; TRDpos=100; 1.1M ev  *PHYS* !!!!


===> 1 crate ; 2 detectors : GEMTRD/FA125 ; GEM/SRS
CODA::  Run_2548 (CODA) GEMTRD:ok; CAL:No; SRS:del=0x40; ROSYBC=0x70 9bin; 10APV;OK; TRDpos=155; 1.5M ev  *PHYS* !!!!
cDAQ::  Run_2567 (cDAQ) GEMTRD:ok; CAL:No; SRS:del=0x40; ROSYBC=0x70 9bin; 10APV;OK; TRDpos=155; 3.2M ev  *PHYS* !!!!

===> new files Mode8 (RAW) and Mode5 (short)
cDAQ / rawmode (8) ; trd_ti_fp.conf ; 300 nA; 650-700Hz ; 16 MB/s
14:09 Run_2633 (cDAQ) GEMTRD:ok; CAL:on; SRS:del=0x41; ROSYBC=0x70 3bin; 10APV;OK; TRDpos=150; Mode8; 250K evt;

cDAQ / Mode5; thr300 / trd_ti_fp_m5.conf ; 300 nA; 650-700Hz ; 7.5 MB/s
14:48 Run_2635 (cDAQ) GEMTRD:ok; CAL:on; SRS:del=0x41; ROSYBC=0x70 3bin; 10APV;OK; TRDpos=50;  Mode5; 250 K evt;
```

Test setup configuration:

- rocFMWPC1 is a TI master (no TSG), with single fa250 board, the last 3 channels contain data from the calorimeter.
- rocTRD1 is a slave with 4 fa125 boards reading GEMTRD (bank 16) also provides SRS/GEM data - bank 17.


Example of reading evio file: 

```bash
/mnt/c/eic/data/2023-02_ml4fpga_trd_data/hd_rawdata_002539_000.evio
```


## TCP test sender and receiver

There are two test executables built with the project that could be used for testing

- tcp_sender - can send dummy evio events through tcp
- tcp_receiver - can receive them

To make test loop: sender sends data to receiver - drinks coffee (or what it does): 

```bash
# In one terminal/process
tcp_receiver

# In another terminal/process
tcp_sender -req=ex -cmd=send -host=localhost:20249
```

## Run flags examples

### CDAQ evio example analysis

Run parameters:

```
jana4ml4fpga
-Pplugins=log,root_output,CDAQfile,example_evio_analysis
-Pjana:debug_plugin_loading=1
-PCDAQEVIOFileSource:LogLevel=trace
-Pexample_evio_analysis:LogLevel=trace
-Pjana:timeout=0
-Pjana:nevents=10
-Pnthreads=1
-Phistsfile=/home/romanov/eic/JANA4ML4FPGA/cmake-build-debug/test.root
/mnt/work/data/2023-03-03-trd-data/hd_rawdata_002633_000.evio
```

### Read EVIO and save to flat root file

```sh
jana4ml4fpga
-Pplugins=log,root_output,CDAQfile,flat_tree
-Pnthreads=1
-Phistsfile=output_file_name.root
hd_rawdata_002633_000.evio          # <= input file
```

### CDAQ TCP via EVIO

*(please refer to the [cdaq plugin](src/plugins/cdaq) for details on writing events to TCP using the `cdaq` plugin)*

```sh
jana4ml4fpga
-Pplugins=log,root_output,flat_tree,cdaq
-Pjana:timeout=0
-Pnthreads=1
tcp-cdaq-evio
```

## Flags


#### JANA

```sh
# Write extended info where plugins are loaded from  
-Pjana:debug_plugin_loading=1

# <plugin>:LogLevel usually controls verbosity level of a plugin
-Pflat_tree:LogLevel=trace

# Switches off jana internal watchdog timer
# Setting timeout=0 is needed if processing threads stops, 
# i.e. when pausing on debug breakpoint or waiting for connection, etc. 
-Pjana:timeout=0

# Set number of events to process
-Pjana:nevents=10000

# Set number of events to skip
-Pjana:nskip=10000

# Save events to EVIO file
-PEVIO:output_file=/file/name.evio


```

#### SRS

```sh
# (!) Important (!) Number of SRS window raw data time all_samples
-Pdaq:srs_window_raw:ntsamples=9

# (!) Important (!) GEM mapping file
-Pgemrecon:mapping=/tmp/JANA4ML4FPGA/scripts/db/2023_mapping_fermilab3.cfg

# Logging flags
-Pgemrecon:LogLevel=info
-Pgemrecon:ClusterF:LogLevel=info


```

#### Data Quality Monitor (DQM)

```sh
# Min event number, when DQM fill plots.
-Pdqm:min_event

# Max event number, when DQM fill plots. 0 - no cap
-Pdqm:max_event

# DQM work every x events: 1 - every event, 3 - once every 3 events, etc.
-Pdqm:every
```



