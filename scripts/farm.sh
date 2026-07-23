#!/bin/bash

TYPE=$1

if [[ $TYPE != "all" && $TYPE != "farm"  && $TYPE != "raid" ]] ; then
    echo " use: $0 <all,farm,raid> [ command ] "
    exit 1
fi


GNUM=""
#GNUM="`seq  40  42`  44  `seq  103 107`  `seq  109 113`  `seq  115 117` 120 122 123  `seq  125 127` `seq  129 133`"
GNUM=" 112  114  115 116 117 "

FARM=

RAID=" gluonraid3 gluonraid4 gluonraid5 gluonraid6"

NN=0

for ff in $GNUM  ; do 
    FARM="$FARM gluon$ff"
    #echo "------------>  gluon${ff} <----------------------"
    #ping -c 1 -W1 gluon${ff} | grep -e icmp_seq 
done

GLUONS=

if  [[ $TYPE == "all" ]] ; then
    GLUONS="$FARM $RAID "
elif [[ $TYPE == "farm" ]] ; then
    GLUONS="$FARM "
elif [[ $TYPE == "raid" ]] ; then
    GLUONS="$RAID "
fi



shift

#echo " exec: command: $* "
echo " $GLUONS "

if [[ x$1 == "x" ]] ; then
    exit 0
fi

sleep 5

for ff in $GLUONS ; do

    echo "------------>  exec for ${ff} $* <----------------------"
    ssh $ff $* 2>/dev/null

    NN=$(( $NN +1 ))
    
done


echo "total nodes: $NN "

exit 0
