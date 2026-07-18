#!/bin/bash

# 4369 4378 4387 4403
# 4375, no radiator, 5.963M events
# 4324, NHVF, 5.16M events
# 4381, double radiators (NHVF), 5.5M events
# 4376, foam, 5.724M events
# 4393, plastic, 2.3M events
# 4403, copper shielding (NHVF), 4.19M events
# CERN: 5213 5221 5231 5232 5253 5254 5255 5256 5260 5261 5265 5266 5273 5274 5275 5276 5277 5278 5281 5282 5284(2) 5304 5306 5313 5159 5215 5217 5218 5219 5220
 
RUN=${1-none}
NTYPE=${2-none}
SRSBIN=${3-3}

if [[ $RUN == "none" || $NTYPE == "none" ]] ; then
    echo " use: $0 <runnum> <farm,raid> [SRS bin] "
    exit 1
fi


EXE=jana4ml4fpga

source ./setup_env.sh
pwd

RUNNUM=$(printf '%06d' ${RUN} )

RUN0="DATA4/hd_rawdata_${RUNNUM}_000.evio"
ls -l $RUN0
RETC=$?
echo "rc=$RETC"
if [[ $RETC != "0" ]] ; then
    echo "Error run number: $RUN "
    exit 1
fi


RUNLIST="`ls -1 DATA4/hd_rawdata_${RUNNUM}_???.evio `"
RUNLIST=$(echo $RUNLIST|tr -d '\n')
echo "run list: $RUNLIST "
NRUN=`echo $RUNLIST | wc -w`
echo " NRUN=$NRUN"

NFARM=`./farm.sh farm | wc -w`
NRAID=`./farm.sh raid | wc -w`
MAXPROC=0

if   [[ $NTYPE == "farm" ]] ; then    
    NODES=`./farm.sh farm`
    MAXPROC=4
elif [[ $NTYPE == "raid" ]] ; then  
    NODES=`./farm.sh raid`
    MAXPROC=10
else
    echo "error: unknow NTYPE=$NTYPE "
    exit 1
fi
    
NNOD=`echo $NODES | wc -w`
echo $NODES
echo " NNOD=$NNOD"

NPROC0=$(( $NRUN / $NNOD + 1 ))
NREST=$(( $NRUN % $NNOD ))
echo " number processes per node = $NPROC0 NREST=$NREST"

sleep 5

FILENUM=0
for node in $NODES ; do
    for iproc in `seq 1 $NPROC0` ; do
	#    xterm -e ssh -Y  $node "which top && sleep 10" &
	#    xterm -e ssh -Y  $node  &
	#./farm.sh  $node "ps -ef | grep $EXE "
	echo "iproc=$iproc :: Start proc on $node : ./run_evio.sh ${RUN}  0 SRS ${SRSBIN} $FILENUM "
#++++++++++++++++++++++++++++++++++++++++++++++++++++++++
set -x
ssh $node "cd JANA3; source setup_env.csh; nohup ./run_evio.sh ${RUN}  0 SRS ${SRSBIN} $FILENUM  > LOG/${RUN}_${FILENUM}_${node}.log " 2>/dev/null &
set +x
#++++++++++++++++++++++++++++++++++++++++++++++++++++++++	
	FILENUM=$(( $FILENUM + 1 ))
	if (( $FILENUM >= $NRUN )) ; then
	    break
	fi
    done
done

#-------------------------------
sleep 5
#./farm.sh  $NTYPE "ps -ef | grep $EXE | grep -v grep | wc -l "
./farm.sh  $NTYPE "ps -ef | grep $EXE | grep -v grep | wc -l "

# ./farm.sh farm "ps -ef | grep jana4ml4fpga  | grep -v grep | wc -l "
# ./farm.sh raid "ps -ef | grep jana4ml4fpga  | grep -v grep | wc -l "
# ./farm.sh farm "ps -ef | grep jana4ml4fpga | grep -v grep  | cut -c400- "

# ls -1 ROOT/Run_*_*.root | sort | cut -f2 -d_ | uniq

# ./farm.sh farm "killall  jana4ml4fpga  "
# ./farm.sh raid "killall  jana4ml4fpga  "


exit 0

#====================================================================================================

LISTS=${1-file_list}

while read lst  ; do

    echo " use list = $lst "

#    while true ; do
#        proc_list=`ps -ef | grep -v grep | grep "mss/halld" | awk '{printf("%s %d %d \n",$1,$2,$3) }'`
#        echo "PROC_LIST=$proc_list"
#        NJOBS=`ps -ef | grep java | grep jasmine | grep -v grep | grep jget`
#        if [[ x"$proc_list" == "x" ]] ; then
#            break
#        else 
#            sleep 10
#        fi
#    done

    echo " take next list $lst "

#    sleep 5

    echo "sh run_evio.sh $lst  0 SRS ${SRSBIN} "
   nohup  ./run_evio.sh $lst 0 SRS ${SRSBIN}  > LOG/$lst.log &

#    sleep 5

done < ${LISTS}

