// REFERENCE: https://github.com/google/nccl-fastsocket.git


#include "common.h"

#include "net_dedup.h"
#include "net_dedup_coll.h"


Net_Dedup_State net_dedup_state;


const ncclNet_v8_t ncclNetPlugin_v8 = {
  .name = "DEDUP",
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

const ncclCollNet_v8_t ncclCollNetPlugin_v8 = {
  .name = "DEDUP_COLL",
  .init = netDedup_init,
  .devices = netDedup_devices,
  .getProperties = netDedupColl_getProperties_v8,
  .listen = netDedup_listen,
  .connect = netDedupColl_connect,
  .reduceSupport = netDedupColl_reduceSupport,
  .regMr = netDedupColl_regMr_v8,
  .regMrDmaBuf = netDedupColl_regMrDmaBuf,
  .deregMr = netDedupColl_deregMr,
  .iallreduce = netDedupColl_iallreduce,
  .iallgather = netDedupColl_iallgather,
  .ireducescatter = netDedupColl_ireducescatter,
  .iflush = netDedupColl_iflush,
  .test = netDedupColl_test,
  .closeColl = netDedupColl_closeColl,
  .closeListen = netDedupColl_closeListen,
};



const ncclNet_v7_t ncclNetPlugin_v7 = {
  .name = "DEDUP",
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

const ncclCollNet_v7_t ncclCollNetPlugin_v7 = {
  .name = "DEDUP_COLL",
  .init = netDedup_init,
  .devices = netDedup_devices,
  .getProperties = netDedupColl_getProperties_v7,
  .listen = netDedup_listen,
  .connect = netDedupColl_connect,
  .reduceSupport = netDedupColl_reduceSupport,
  .regMr = netDedupColl_regMr_v7,
  .regMrDmaBuf = netDedupColl_regMrDmaBuf,
  .deregMr = netDedupColl_deregMr,
  .iallreduce = netDedupColl_iallreduce,
  .iflush = netDedupColl_iflush,
  .test = netDedupColl_test,
  .closeColl = netDedupColl_closeColl,
  .closeListen = netDedupColl_closeListen,
};

