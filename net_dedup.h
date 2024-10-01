#ifndef NET_DEDUP_H
#define NET_DEDUP_H

#include "common.h"
#include "nccl_net_v8.h"

#include "net_device.h"

#include "fingerprint_cache.h"
#include "fingerprint_table.h"
#include "fingerprint.h"


typedef struct net_dedup_state {
	int num_net_devices;
	Net_Socket_Dev net_devices[MAX_NET_DEDUP_DEVS];
	Fingerprint_Cache * global_fingerprint_cache;
} Net_Dedup_State;


extern Net_Dedup_State net_dedup_state;

// EXPORTED NCCL FUNCTIONS USED AS PLUGIN!


ncclResult_t netDedup_init(ncclDebugLogger_t logFunction);
ncclResult_t netDedup_devices(int * ndev);
ncclResult_t netDedup_getProperties(int dev, ncclNetProperties_v8_t * props);
ncclResult_t netDedup_listen(int dev, void * handle, void ** listenComm);
ncclResult_t netDedup_connect(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v8_t** sendDevComm);
ncclResult_t netDedup_accept(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v8_t** recvDevComm);
ncclResult_t netDedup_regMr(void * comm, void * data, size_t size, int type, void ** mhandle);
ncclResult_t netDedup_regMrDmaBuf(void* comm, void* data, size_t size, int type, uint64_t offset, int fd, void** mhandle);
ncclResult_t netDedup_deregMr(void * comm, void * mhandle);
ncclResult_t netDedup_isend(void * sendComm, void * data, int size, int tag, void * mhandle, void ** request);
ncclResult_t netDedup_irecv(void * recvComm, int n, void ** data, int * sizes, int * tags, void ** mhandles, void ** request);
ncclResult_t netDedup_iflush(void * recvComm, int n, void ** data, int * sizes, void ** mhandles, void ** request);
ncclResult_t netDedup_test(void * request, int * done, int * sizes);
ncclResult_t netDedup_closeSend(void * sendComm);
ncclResult_t netDedup_closeRecv(void * recvComm);
ncclResult_t netDedup_closeListen(void * listenComm);
ncclResult_t netDedup_getDeviceMr(void * comm, void * mhandle, void ** dptr_mhandle);
ncclResult_t netDedup_irecvConsumed(void * recvComm, int n, void * request);


#endif