#!/bin/bash
cd ~/tidy
CHECKS=$(cat checks.txt)
start=$(date +%s)
mapfile -t R < chunk$1.txt
timeout 580 run-clang-tidy-19 -p ~/tidy/db -j 6 -quiet -checks="$CHECKS" -header-filter="^$HOME/openba-review/src/" "${R[@]}" > out/chunk$1.txt 2>&1
echo "chunk$1 rc=$? secs=$(( $(date +%s)-start ))" | tee -a log.txt
