#!/bin/bash
# assumes this shell script will be run as: . ./script RANK
export NCCL_DEBUG="INFO"
export NCCL_DEBUG_FILE=$1"_nccl_debug_file.txt"

## use the nccl plugin
export NCCL_NET_PLUGIN="dedup"

## setting max parallel requests per send/recv comm
export NCCL_NET_MAX_REQUESTS=1


## to ensure cublas/cudnn reproducibility
export CUBLAS_WORKSPACE_CONFIG=":4096:8"


