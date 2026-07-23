#!/bin/bash

# 4369 4378 4387 4403
# 4375, no radiator, 5.963M events
# 4324, NHVF, 5.16M events
# 4381, double radiators (NHVF), 5.5M events
# 4376, foam, 5.724M events
# 4393, plastic, 2.3M events
# 4403, copper shielding (NHVF), 4.19M events

# CERN: 5213 5221 5231 5232 5253 5254 5255 5256 5260 5261 5265 5266 5273 5274 5275 5276 5277 5278 5281 5282 5284(2) 5304 5306 5313 5159 5215 5217 5218 5219 5220
 
#RUNLIST=" 4351 4353 4365 "
#RUNLIST=" 130512 "
#RUNLIST="6295 6296 6297 6302 6303 6304 6314 6315 6316 6317 6318 6319 6320 6357 6361 6365 6366  6382 6383 6384 6385 6387 6388 6389 6390 6391 6392 6393 6394"
RUNLIST="6208 6209 6210 6211 6212 6213 6214 6215 6216 6217 6218 6238 6239 6240 6241 6242 6243 6244 6245 6246 6247 6248 6249 6250 6251 6252 6253 6254 6255 6256 6257 6258 6259 6260 6261 6262 6263 6278 6321 6325 6326 6327 6328 6335 6336 6337 6338 6339 6340 6341 6342 6343 6344 6345 6346 6347 6348 6357 6361 6366"

NTYPE=${1-raid}
SRSBIN=${2-3}

if [[ $NTYPE == "none" ]] ; then
    echo " use: $0  <farm,raid> [SRS bin] "
    exit 1
fi


EXE=jana4ml4fpga

source ./setup_env.sh
pwd



#RUNLIST="`ls -1 DATA3/hd_rawdata_${RUNNUM}_???.evio `"
#RUNLIST=$(echo $RUNLIST|tr -d '\n')
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

ANODES=( $NODES )

for ai in `seq 0 $(($NNOD -1 ))` ; do 
    echo " use node $ai ${ANODES[$ai]}"
done

NPROC0=$(( $NRUN / $NNOD + 1 ))
NREST=$(( $NRUN % $NNOD ))
echo " number processes per node = $NPROC0 NREST=$NREST"

sleep 5

inode=0
node=${ANODES[$inode]}
iproc=0

for run in $RUNLIST ; do

    echo " next run = $run "
    RUNNUM=$(printf '%06d' ${run} )
    
    RUN0="DATA4/hd_rawdata_${RUNNUM}_000.evio"
    ls -l $RUN0 > /dev/null
    RETC=$?
    echo "rc=$RETC"
    if [[ $RETC != "0" ]] ; then
	echo "Error run number: $RUN "
	continue
    fi	
	
    echo "iproc=$iproc :: Start proc on $node : ./run_evio.sh ${run}  0 SRS ${SRSBIN} "
#++++++++++++++++++++++++++++++++++++++++++++++++++++++++
set -x
ssh $node "cd JANA3; source setup_env.csh; nohup ./run_evio.sh ${run}  0 SRS ${SRSBIN} > LOG/${run}_${node}.log " 2>/dev/null &
set +x
#++++++++++++++++++++++++++++++++++++++++++++++++++++++++	

   iproc=$(( $iproc + 1 ))
#   if (( $iproc == $MAXPROC )) ; then
   if (( $iproc == $NPROC0 )) ; then
       inode=$(( $inode + 1 ))
       if (( $inode == $NNOD )) ; then
	   inode=0
       fi
       node=${ANODES[$inode]}
       iproc=0
   fi
done

#-------------------------------
sleep 5
#./farm.sh  $NTYPE "ps -ef | grep $EXE | grep -v grep | wc -l "

while true ; do
    ./farm.sh  $NTYPE "ps -ef | grep $EXE | grep -v grep | wc -l "
    sleep 5
done

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

