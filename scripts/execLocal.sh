#!/bin/bash

if [ -z "$1" ]
then
    echo "please specify task to execute"
    exit 1
fi
if [ -z "$2" ]
then
    echo "please specify # of reqeusts to provide"
    exit 1
fi
if [ -z "$3" ]
then
    echo "please specify batch size"
    exit 1
fi
if [ -z "$4" ]
then
    echo "please specify mean reqeust arrival time (in seconds)"
    exit 1
fi

if [ -z "$5" ]
then
    echo "no thread cap specified, using default setup"
else
    echo "using $5 as thread cap"
    cap=$5
	SERVER_PID=$(echo get_server_list | nvidia-cuda-mps-control)

	if [ -z $SERVER_PID ]
	then
        echo "there is no mps server, make sure that you have turned on MPS Server, proxy not turned on"
        exit
	fi

    # Enable MPS
    echo "enabling MPS"
    export CUDA_VISIBLE_DEVICES=0
    export CUDA_MPS_PIPE_DIRECTORY=/tmp/mps_0
    export CUDA_MPS_LOG_DIRECTORY=/tmp/mps_log_0
    sudo nvidia-smi -i 0 -c EXCLUSIVE_PROCESS
    nvidia-cuda-mps-control -d
    unset CUDA_VISIBLE_DEVICES
    unset CUDA_MPS_PIPE_DIRECTORY
    unset CUDA_MPS_LOG_DIRECTORY

    # Export prefix when using MPS uncapping
    # export_prefix="export CUDA_MPS_ENABLE_PER_CTX_DEVICE_MULTIPROCESSOR_PARTITIONING=0 && \
    #                 export CUDA_MPS_PIPE_DIRECTORY=/tmp/mps_${device_id_arg} && \
    #                 export CUDA_VISIBLE_DEVICES=0"

    # Export prefix when using MPS capping

    export_prefix="export CUDA_MPS_ENABLE_PER_CTX_DEVICE_MULTIPROCESSOR_PARTITIONING=1 && \
                            export CUDA_MPS_ACTIVE_THREAD_PERCENTAGE=${cap} && \
                            export CUDA_MPS_PIPE_DIRECTORY=/tmp/mps_0 && \
                            export CUDA_VISIBLE_DEVICES=0"
    

	# echo set_active_thread_percentage $SERVER_PID 100 | nvidia-cuda-mps-control
	# sleep 3

	# echo set_active_thread_percentage $SERVER_PID $cap | nvidia-cuda-mps-control

fi

RES_DIR=$PWD/../resource/ # beware! '/' at the end is required!
BUILD_DIR=$PWD/../bin/
task=$1
req=$2
batch=$3
mean=$4

cmd="${export_prefix} && \
    $BUILD_DIR/standalone_inference --task $task --taskfile $RES_DIR/models/$task.pt --requests $req \
    --batch $batch --mean $mean --input $RES_DIR/$input_tx --input_config_json $RES_DIR/input_config.json"

eval $cmd