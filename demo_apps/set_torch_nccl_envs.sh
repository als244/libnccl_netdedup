#!/bin/bash
# assumes this shell script will be run as: . ./script RANK
#export TORCH_DISTRIBUTED_DEBUG="INFO"
#export TORCH_NCCL_TRACE_BUFFER_SIZE=1000
#export TORCH_NCCL_ENABLE_MONITORING=1
#export TORCH_NCCL_HEARTBEAT_TIMEOUT_SEC=10
#export TORCH_NCCL_ENABLE_TIMING=1
#export TORCH_NCCL_DEBUG_INFO_PIPE_FILE=$1"_dump.pipe"
#export TORCH_NCCL_DEBUG_INFO_TEMP_FILE=$1"_dump_file.txt"
export NCCL_DEBUG="INFO"
export NCCL_DEBUG_FILE=$1"_nccl_debug_file.txt"

## enabling collnet (default is off)
#export NCCL_COLLNET_ENABLE=1


### FORCING nccl to use the dedup plugin
### this overides the "netName" within every communicator (set within torch dist)
### going to fail until we have correct implementation...
### this is referring the "name" within the struct within the plugin...
# export NCCL_NET="Dedup"


## attempt to use plugin but fall back to either Net = IB or Net = Socket
## will not o
export NCCL_NET_PLUGIN="dedup"


## to ensure cublas/cudnn reproducibility
export CUBLAS_WORKSPACE_CONFIG=":4096:8"

## disabling ib (without specifying NCCL_NET), leads to using NET=Socket
# export NCCL_IBEXT_DISABLE=1
# export NCCL_IB_DISABLE=1

