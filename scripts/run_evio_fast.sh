#!/bin/bash
# Parallel (multithreaded) SRS readout+reconstruction using the CDAQfileMT mmap
# source. Same output as run_evio.sh SRS mode, but:
#   -Pplugins=CDAQfileMT   (mmap EVIO source) instead of the serial CDAQfile
#   -Pevio:parallel=1      (MmapEvioSource: EVIO deserialization runs in parallel)
#   -Pnthreads=N           (worker threads)
# GLIBC_TUNABLES (set by setup_env.sh) is required for the multithreaded speedup.
#
# flat_tree auto-switches to multithreaded output when nthreads != 1 (per-thread
# trees merged via TBufferMerger); entries are unordered - sort by event_number.
read -r -d '' HELP_TEXT << EOH
Parallel SRS processing with the CDAQfileMT mmap source.
Parameters:
- 'RUN'      - run number to process.
- 'MAXEVT'   - max events (0 = all). Default 0.
- 'SRSBIN'   - number of SRS time bins. Default 3.
- 'NTHREADS' - worker threads. Default 8.
- 'FILE'     - optional single file number (zero-padded to 3 digits).
Usage:
run_evio_fast.sh [run_num] [max events] [srsbin] [nthreads] [file_num]
EOH

RUN=${1-help}
MAXEVT=${2-0}
SRSBIN=${3-3}
NTHREADS=${4-8}
FILE=$5

if [[ $RUN == "help" ]] || [[ $RUN == "--help" ]] ; then
    echo -e "\n$HELP_TEXT\n"
    exit 0
fi

# SRS mapping by run range (kept in sync with run_evio.sh)
SRS_MAPPING=
if   ((3000 < $RUN && $RUN <= 3156)) ; then SRS_MAPPING="db/2023_fermi_SRSmap0.cfg"
elif ((3156 < $RUN && $RUN <= 3261)) ; then SRS_MAPPING="db/2023_fermi_SRSmap1.cfg"
elif ((3261 < $RUN && $RUN <= 3299)) ; then SRS_MAPPING="db/2023_fermi_SRSmap2.cfg"
elif ((4700 < $RUN && $RUN <  7000)) ; then SRS_MAPPING="db/2024_mapping_CERN.cfg"
elif ((8000 < $RUN && $RUN < 10000)) ; then SRS_MAPPING="db/2026_mapping_PS.cfg"
fi
echo "SRS_MAPPING = $SRS_MAPPING"

RUNNUM=$(printf '%06d' ${RUN})

if [[ x$FILE == "x" ]] ; then
    FILELIST="`/bin/ls /gluonraid3/data2/rawdata/trd/DATA/hd_rawdata_${RUNNUM}_*.evio`"
    ROOT_FILENAME=ROOT/Run_${RUNNUM}.root
    echo " Process All files for RUN=$RUN  ROOT_FILENAME=$ROOT_FILENAME "
else
    FILENUM=$(printf '%03d' ${FILE})
    FILELIST="`/bin/ls /gluonraid3/data2/rawdata/trd/DATA/hd_rawdata_${RUNNUM}_${FILENUM}.evio`"
    ROOT_FILENAME=ROOT/Run_${RUNNUM}_${FILENUM}.root
    echo " Process file = $FILELIST  ROOT_FILENAME=$ROOT_FILENAME "
fi

echo "FILELIST = $FILELIST"
echo "RUN      = $RUN"
echo "SRSBIN   = $SRSBIN"
echo "NTHREADS = $NTHREADS"
[[ $MAXEVT == 0 ]] && echo "MAXEVT   = ALL" || echo "MAXEVT   = $MAXEVT"
echo " MODE = SRS (parallel / CDAQfileMT)"
sleep 1

set -x
jana4ml4fpga -Pplugins=CDAQfileMT,flat_tree,root_output,gemrecon2 \
-Pevio:parallel=1 \
-Pnthreads=${NTHREADS} \
-Pjana:nevents=${MAXEVT} \
-Pjana:timeout=0 \
-Pdaq:srs_window_raw:ntsamples=${SRSBIN} \
-Plog:level=info \
-Pevio:log=info \
-Pgem:log=info \
-Pgemrecon:mapping=${SRS_MAPPING} \
-Phistsfile=${ROOT_FILENAME}  $FILELIST
set +x
