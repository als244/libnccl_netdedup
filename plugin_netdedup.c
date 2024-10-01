// REFERENCE: https://github.com/google/nccl-fastsocket.git


#include "common.h"

#include "net_dedup.h"


Net_Dedup_State net_dedup_state;


const ncclNet_v8_t ncclNetPlugin_v8 = {
  .name = "netdedup",
  .init = netDedup_init,
  .devices = netDedup_devices,
  .getProperties = netDedup_getProperties,
  .listen = netDedup_listen,
  .connect = netDedup_connect,
  .accept = netDedup_accept,
  .regMr = netDedup_regMr,
  .regMrDmaBuf = netDedup_regMrDmaBuf,
  .deregMr = netDedup_deregMr,
  .isend = netDedup_isend,
  .irecv = netDedup_irecv,
  .iflush = netDedup_iflush,
  .test = netDedup_test,
  .closeSend = netDedup_closeSend,
  .closeRecv = netDedup_closeRecv,
  .closeListen = netDedup_closeListen,
  .getDeviceMr = netDedup_getDeviceMr,
  .irecvConsumed = netDedup_irecvConsumed,
};


