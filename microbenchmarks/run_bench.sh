#!/bin/bash
make clean
make
power_save=`cpupower frequency-info|grep 'governor "performance"'|wc -l`
if [[ 0 -eq "$power_save" ]]
then
  sudo cpupower frequency-set --governor performance
fi

echo ""
./$1
