#!/bin/bash

RUNLIST=" 5213 5221 5231 5232 5253 5254 5255 5256 5260 5261 5265 5266 5273 5274 5275 5276 5277 5278 5281 5282 5284 5304 5306 5313 5159 5215 5217 5218 5219 5220 "

for run in ${RUNLIST} ; do 

echo "====== Start run = $run ======"
sleep 1

./run_evio.sh $run 0 SRS 3 0

done

