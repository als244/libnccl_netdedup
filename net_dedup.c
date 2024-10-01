#include "net_dedup.h"





// initializes network
ncclResult_t netDedup_init(ncclDebugLogger_t logFunction) {
	
	int num_net_devices = init_net_devices(net_dedup_state.net_devices);
	net_dedup_state.num_net_devices = num_net_devices;
	return ncclSuccess;
}


// returns number of devices
ncclResult_t netDedup_devices(int * ndev) {
	
	*ndev = net_dedup_state.num_net_devices;
	return ncclSuccess;

}


// queries devices properties
ncclResult_t netDedup_getProperties(int dev, ncclNetProperties_v8_t * props) {
	
	if (dev >= net_dedup_state.num_net_devices){
		return ncclInvalidUsage;
	}

	Net_Socket_Dev q_dev = net_dedup_state.net_devices[dev];

	props -> name = q_dev.if_name;

	// this should only be for ib devices
	props -> guid = 0;
	// we could query /sysfs if needed to find this
	props -> pciPath = NULL;

	// using sockets means only host pointers
	props -> ptrSupport = NCCL_PTR_HOST;
	// registering memory will be seen across whole system becuase
	// we are using the fingerprint cache and shared memory
	props -> regIsGlobal = 1;


	props -> speed = q_dev.if_speed;
	// netdev interface port number doesn't matter
	// default port numbers start at 1 for ib so will do same
	props -> port = 1;


	// will use nccl default
	props -> latency = 0;


	props -> maxComms = MAX_COMMS_NET_DEDUP_SOCKET_DEV;

	// ensure that we are not handling more than 1 receive at a time for now...
	props -> maxRecvs = 1;


	props->netDeviceType = NCCL_NET_DEVICE_HOST;
  	
	// not sure what this means...? API version? Something else for the proxy stuff..?
  	props->netDeviceVersion = 0;


  	return ncclSuccess;
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