# FPGA connection and exchange


```mermaid
flowchart TB
    classDef alg fill:#44cc;
    classDef ext fill:#cc44;
    classDef col fill:#cc66ff;
  
    subgraph JANA4ML4FPGA
        direction TB
        Df125WindowRawData(<strong>Data from EVIO</strong><br/><i>Df125WindowRawData</i>)
        Df125WindowRawData --> F125ClusterFactory[<strong>Clustering</strong>:<br/><i>F125ClusterFactory</i>]:::alg
        
        F125ClusterFactory --> F125Cluster(<strong>Array of clusters & clustering info</strong><br/><i>F125Cluster<br>F125ClusterContext</i>)
        F125Cluster --> FpgaExchangeFactory[<strong>FPGA data exchange</strong>:<br/><i>FpgaExchangeFactory</i>]:::alg
        FpgaExchangeFactory --> FpgaHitsToTrack(<strong>Hits to track assoc. & track fits</strong><br/><i>FpgaHitsToTrack<br>FpgaTrackFit</i>)
        FpgaHitsToTrack --> FpgaDqmProcessor[<strong>Plotting histograms</strong><br/><i>FpgaDqmProcessor</i>]:::alg
        F125Cluster -.-> FpgaDqmProcessor
    end

    FpgaExchangeFactory <====>  FPGAProcessor(((<strong>F P G A</strong>))):::ext
```

# Run files

On CERN 2024-07 beam tests, file Data 5083

```bash
/home/hdtrdops/DATA/hd_rawdata_005083_000.evio
```

fpga2file.sh

```bash
#!/bin/bash

set -x

jana4ml4fpga \
  -Pplugins=CDAQfile,flat_tree,root_output,dqm,fpgacon \
  -Pjana:nevents=1000 \
  -Pdaq:srs_window_raw:ntsamples=3 \
  -Pjana:debug_plugin_loading=1 \
  -Pfpgacon:port=20250 \
  -Pfpgacon:dedx_threshold=200  \
  -Pjana:timeout=0 \
  -Pdqm:max_event=1  \
  /home/hdtrdops/DATA/hd_rawdata_005083_000.evio
```

daq2file.sh

```bash
#!/bin/bash

set -x

jana4ml4fpga \
  -Pplugins=cdaq,flat_tree,root_output,print_evio \
  -Pjana:nevents=10000 \
  -Pcdaq:port=20248 \
  -Pjana:timeout=0  \
  -Pdaq:srs_window_raw:ntsamples=3 \
  -Pevio:LogLevel=trace \
  -Pgemrecon:LogLevel=trace \
  -Phistsfile=daq_receive.root \
  tcp-cdaq-evio
```

daq2fpga.sh

```sh
#!/bin/sh

set -x

jana4ml4fpga \
  -Pplugins=cdaq,flat_tree,root_output,dqm,fpgacon \
  -Pjana:nevents=10000 \
  -Pdaq:srs_window_raw:ntsamples=3 \
  -Pcdaq:port=20248 \
  -Pjana:debug_plugin_loading=1 \
  -Pfpgacon:port=20250 \
  -Pfpgacon:dedx_threshold=200  \
  -Pdqm:max_event=1000  \
  -Pjana:timeout=0  \
  -Phistsfile=/home/hdtrdops/romanov/daq_fpga_10k_001.root \
  tcp-cdaq-evio
```
