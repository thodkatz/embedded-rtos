#!/bin/bash

queuesize=(20 50 200 1000)
loop=2000
producers=(1 2 4)
consumers=(1 2 4)

echo "queuesize,loopsize,producers,consumers,time,item" > results.csv
for i in ${queuesize[@]}; do
    make local QUEUESIZE=$i
    for j in ${producers[@]}; do 
        for k in ${consumers[@]}; do
            ./bin/local $loop $j $k 
        done
    done
done