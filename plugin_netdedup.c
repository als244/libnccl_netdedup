// REFERENCE: https://github.com/google/nccl-fastsocket.git


#include "common.h"

#include "net_dedup.h"


Net_Dedup_State net_dedup_state;


const ncclNet_v8_t ncclNetPlugin_v8 = {
  .name = "netdedup_v8",
  .init = netDedup_init,
  .devices = netDedup_devices,
  .getProperties = netDedup_getProperties_v8,
  .listen = netDedup_listen,
  .connect = netDedup_connect_v8,
  .accept = netDedup_accept_v8,
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


const ncclNet_v7_t ncclNetPlugin_v7 = {
  .name = "netdedup_v7",
  .init = netDedup_init,
  .devices = netDedup_devices,
  .getProperties = netDedup_getProperties_v7,
  .listen = netDedup_listen,
  .connect = netDedup_connect_v7,
  .accept = netDedup_accept_v7,
  .regMr = netDedup_regMr_v7,
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

