#ifndef NET_DEDUP_COLL_H
#define NET_DEDUP_COLL_H

#include "common.h"
#include "net_dedup.h"

ncclResult_t netDedupColl_init(ncclDebugLogger_t logFunction);
ncclResult_t netDedupColl_devices(int * ndev);
ncclResult_t netDedupColl_getProperties_v8(int dev, ncclNetProperties_v8_t * props);
ncclResult_t netDedupColl_listen(int dev, void * handle, void ** listenComm);
ncclResult_t netDedupColl_connect(void* handles[], int nranks, int rank, void* listenComm, void** collComm);
ncclResult_t netDedupColl_reduceSupport(ncclDataType_t dataType, ncclRedOp_t redOp, int* supported);
ncclResult_t netDedupColl_regMr_v8(void* collComm, void* data, size_t size, int type, void** mhandle);
ncclResult_t netDedupColl_regMrDmaBuf(void* collComm, void* data, size_t size, int type, uint64_t offset, int fd, void** mhandle);
ncclResult_t netDedupColl_deregMr(void* collComm, void* mhandle);
ncclResult_t netDedupColl_iallreduce(void* collComm, void* sendData, void* recvData, int count,
      ncclDataType_t dataType, ncclRedOp_t redOp, void* sendMhandle, void* recvMhandle, void** request);
ncclResult_t netDedupColl_iallgather(void* collComm, void* sendData, int nRecvParts, ncclNetSGE_v8_t* recvParts,
                             size_t bytesPerRank, size_t windowOffset, size_t windowBytes,
                             void* sendMhandle, void** request);
ncclResult_t netDedupColl_ireducescatter(void* collComm, int nSendParts, ncclNetSGE_v8_t* sendParts, void* recvData,
                                 size_t bytesPerRank, size_t windowOffset, size_t windowBytes,
                                 ncclDataType_t dataType, ncclRedOp_t redOp,
                                 void* recvMhandle, void** request);
ncclResult_t netDedupColl_iflush(void* collComm, void* data, int size, void* mhandle, void** request);
ncclResult_t netDedupColl_test(void* request, int* done, int* size);
ncclResult_t netDedupColl_closeColl(void* collComm);
ncclResult_t netDedupColl_closeListen(void* listenComm);


ncclResult_t netDedupColl_getProperties_v7(int dev, ncclNetProperties_v7_t * props);
ncclResult_t netDedupColl_regMr_v7(void* collComm, void* data, int size, int type, void** mhandle);

#endif