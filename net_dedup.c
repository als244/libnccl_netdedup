#include "net_dedup.h"


// initializes network
ncclResult_t netDedup_init(ncclDebugLogger_t logFunction) {
	return ncclInvalidUsage;
}


// returns number of devices
ncclResult_t netDedup_devices(int * ndev) {
	return ncclInvalidUsage;
}


// queries devices properties
ncclResult_t netDedup_getProperties(int dev, ncclNetProperties_v8_t * props) {
	return ncclInvalidUsage;
}


ncclResult_t netDedup_listen(int dev, void * handle, void ** listenComm) {
	return ncclInvalidUsage;
}

ncclResult_t netDedup_connect(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v8_t** sendDevComm) {
	return ncclInvalidUsage;
}


ncclResult_t netDedup_accept(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v8_t** recvDevComm) {
	return ncclInvalidUsage;
}


ncclResult_t netDedup_regMr(void * comm, void * data, size_t size, int type, void ** mhandle) {
	return ncclInvalidUsage;
}


ncclResult_t netDedup_regMrDmaBuf(void* comm, void* data, size_t size, int type, uint64_t offset, int fd, void** mhandle) {
	return ncclInvalidUsage;
}

ncclResult_t netDedup_deregMr(void * comm, void * mhandle) {
	return ncclInvalidUsage;
}

ncclResult_t netDedup_isend(void * sendComm, void * data, int size, int tag, void * mhandle, void ** request) {
	return ncclInvalidUsage;
}

ncclResult_t netDedup_iflush(void * recvComm, int n, void ** data, int * sizes, void ** mhandles, void ** request) {
	return ncclInvalidUsage;
}

ncclResult_t netDedup_test(void * request, int * done, int * sizes) {
	return ncclInvalidUsage;
}


ncclResult_t netDedup_closeSend(void * sendComm) {
	return ncclInvalidUsage;
}

ncclResult_t netDedup_closeRecv(void * recvComm) {
	return ncclInvalidUsage;
}


ncclResult_t netDedup_closeListen(void * listenComm) {
	return ncclInvalidUsage;
}


ncclResult_t netDedup_getDeviceMr(void * comm, void * mhandle, void ** dptr_mhandle) {
	return ncclInvalidUsage;
}


ncclResult_t netDedup_irecvConsumed(void * recvComm, int n, void * request) {
	return ncclInvalidUsage;
}