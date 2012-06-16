#!/bin/bash

run()
{
    prog=$1
    flags=$2
    input=$3
    var=$4
    nproc=$5

    echo -n $var=$nproc $prog $flags $input CPU0-$[$nproc-1] mem0-$[($nproc-1)/8]" "
    eval "time $var=$nproc numactl --physcpubind 0-$[$nproc-1] --membind 0-$[($nproc-1)/8] ./$prog $flags $input > junk" 2>&1 | tr '\n' ' ' | tr -s ' \t' '  '
    if ! grep Running junk ; then echo ; fi
    rm junk
}

input=
repeat=10
#repeat=1

prog=matmul_novec_goto_c
var=NONE
for flags in 64 ; do
    for nproc in 1 ; do
	for run in `seq $repeat` ; do
	    run $prog "$flags" "$input" "$var" $nproc
	done
    done
done

prog=matmul_novec_goto_wfo
var=NUM_THREADS
for flags in 64 ; do
    for nproc in `seq 1 16` ; do
        for run in `seq $repeat` ; do
	    run $prog $flags "$input" $var $nproc
	done
    done
done

prog=matmul_novec_goto_smpss
var=CSS_NUM_CPUS
for flags in 64 ; do
    for nproc in `seq 1 16` ; do
        for run in `seq $repeat` ; do
	    run $prog $flags "$input" $var $nproc
	done
    done
done
