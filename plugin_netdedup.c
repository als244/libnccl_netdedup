// REFERENCE: https://github.com/google/nccl-fastsocket.git


#include "common.h"

#include "net_dedup.h"
#include "net_dedup_coll.h"

#define PLUGIN_NAME "Dedup"
#define COLL_PLUGIN_NAME "CollDedup"


Net_Dedup_State net_dedup_state;

ncclDebugLogger_t nccl_log_func;

// Setting the same functions to null as in reference NCCL-Socket implementation:
//  Ref: https://github.com/NVIDIA/nccl/blob/master/src/transport/net_socket.cc

const ncclNet_v8_t ncclNetPlugin_v8 = {
  .name = PLUGIN_NAME,
  .init = netDedup_init,
  .devices = netDedup_devices,
  .getProperties = netDedup_getProperties_v8,
  .listen = netDedup_listen,
  .connect = netDedup_connect_v8,
  .accept = netDedup_accept_v8,
  .regMr = netDedup_regMr_v8,
  .regMrDmaBuf = NULL,
  .deregMr = netDedup_deregMr,
  .isend = netDedup_isend,
  .irecv = netDedup_irecv,
  .iflush = netDedup_iflush,
  .test = netDedup_test,
  .closeSend = netDedup_closeSend,
  .closeRecv = netDedup_closeRecv,
  .closeListen = netDedup_closeListen,
  .getDeviceMr = NULL,
  .irecvConsumed = NULL,
};

const ncclNet_v7_t ncclNetPlugin_v7 = {
  .name = PLUGIN_NAME,
  .init = netDedup_init,
  .devices = netDedup_devices,
  .getProperties = netDedup_getProperties_v7,
  .listen = netDedup_listen,
  .connect = netDedup_connect_v7,
  .accept = netDedup_accept_v7,
  .regMr = netDedup_regMr_v7,
  .regMrDmaBuf = NULL,
  .deregMr = netDedup_deregMr,
  .isend = netDedup_isend,
  .irecv = netDedup_irecv,
  .iflush = netDedup_iflush,
  .test = netDedup_test,
  .closeSend = netDedup_closeSend,
  .closeRecv = netDedup_closeRecv,
  .closeListen = netDedup_closeListen,
  .getDeviceMr = NULL,
  .irecvConsumed = NULL,
};

const ncclCollNet_v8_t ncclCollNetPlugin_v8 = {
  .name = COLL_PLUGIN_NAME,
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



const ncclCollNet_v7_t ncclCollNetPlugin_v7 = {
  .name = COLL_PLUGIN_NAME,
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

