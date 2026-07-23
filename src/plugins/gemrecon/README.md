```bash
jana4ml4fpga
-Pplugins=log,root_output,flat_tree,CDAQfile,gemrecon
-Pjana:debug_plugin_loading=1
-PCDaqfile:srs_nsamples=20
-PCDAQEVIOFileSource:LogLevel=trace
-Pexample_evio_analysis:LogLevel=trace
-Pgemrecon:LogLevel=trace
-Pgemrecon:mapping=/home/romanov/eic/JANA4ML4FPGA/src/plugins/gemrecon/db/2019_mapping_GEMTRK.cfg
-Pgemrecon:config=/home/romanov/eic/JANA4ML4FPGA/src/plugins/gemrecon/db/Config.cfg
-Pjana:timeout=0
-Pjana:nevents=50
-Pnthreads=1
-Phistsfile=/home/romanov/eic/JANA4ML4FPGA/cmake-build-debug/test.root
/mnt/work/data/gemtrd/hd_rawdata_001068_000.evio
```


## Sample data

What is sample id: 

```C++
channel +
time_bin*1e3 +
apv*1e6 +
apv_index_on_plane*1e9 +
plane_id*1e12 +
detector_id*1e15+
1e18;
```

If samples are sorted by ID they are guaranteed to be in order: 

```
detector -> plane -> time bin -> apv on plane -> channel num
```

Samples **ARE** stored in root file in this order.

This order makes sample values presentation plane oriented: 

```
[plane][time bin 0][all channels on plane in correct order]
[plane][time bin 1][all channels on plane in correct order]
```

## Map example

```yaml
# readout types:
#   CARTESIAN : planeX sizeX nbOfConnectorsX orientationX <same for Y >
#   1DSTRIPS  : plane size nbOfConnectors orientation
#   UV_ANGLE  : length outerRadius innerRadius planeTop nbOfConnectorsTop orientationTop <same for Bot>
#   PADPLANE  : padPlane padSizeX nbPadX padSizeY nbPadY nbConnectors
#   CMSGEM    : etaSector etaSectorPos etaSectorSize nbConnectors orientation
#################################################################################################
     readoutType    DetType    DetName     Plane   size (mm)  connectors  orientation
#################################################################################################
DET,  CARTESIAN,   STANDARD,   GEMTR1,   GEMTR1X,   409.6,   4,    1,  GEMTR1Y, 51.2,   1,   1
DET,  CARTESIAN,   STANDARD,   GEMTR2,   GEMTR2X,   409.6,   4,    1,  GEMTR2Y, 51.2,   1,   1
#DET,  CARTESIAN,   STANDARD,   GEMTR3,   GEMTR3X,   409.6,   1,    1,  GEMTR3Y, 51.2,   1,   1
###############################################################
#     fecId   adcCh   detPlane  apvOrient  apvIndex    apvHdr #
###############################################################
#
###X->Y is clockwise

#################
#  SRU1 -> GEM1
#################
#
FEC,   0,     10.0.0.2
APV,   0,     0,      GEMTR1X,     0,    0,    1500,     normal
APV,   0,     1,      GEMTR1X,     0,    1,    1500,     normal
APV,   0,     2,      GEMTR1X,     0,    2,    1500,     normal
APV,   0,     3,      GEMTR1X,     0,    3,    1500,     normal
APV,   0,     4,      GEMTR1Y,     0,    0,    1500,     normal
#
# APV,   0,     5,      GEMTR3Y,     0,    0,    1500,     normal
#
APV,   0,     6,      GEMTR2X,     0,    0,    1500,     normal
APV,   0,     7,      GEMTR2X,     0,    1,    1500,     normal
APV,   0,     8,      GEMTR2X,     0,    2,    1500,     normal
APV,   0,     9,      GEMTR2X,     0,    3,    1500,     normal
APV,   0,     10,     GEMTR2Y,     0,    0,    1500,     normal
#
#APV,   0,     11,     GEMTR3X,     0,    0,    1500,     normal
#APV,   0,     12,     GEMTRDY,     0,    0,    1500,     normal
#APV,   0,     13,     GEMTRDY,     0,    1,    1500,     normal
#APV,   0,     14,     GEMTRK1X,     0,    0,    1500,     normal
#APV,   0,     15,     GEMTRK1X,     0,    1,    1500,     normal


```