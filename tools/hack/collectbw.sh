#!/usr/bin/env bash

FILES=$(ls $1)
for f in $FILES 
do
  BATCH=$(grep -oP "\-batch\K\d*" <<< "$f")
  BW=$(tail -n 1 $f)
  echo $BATCH $BW
done
