#!/usr/bin/env bash

FILES=$(ls $1)
for f in $FILES 
do
  tail -n 1
done
