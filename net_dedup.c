#include "net_dedup.h"


// initializes network
ncclResult_t netDedup_init(ncclDebugLogger_t logFunction) {
	
	pid_t pid = getpid();
	printf("[Process %d] Initially loaded net dedup nccl plugin!\n", pid);

	int num_net_devices = init_net_devices(net_dedup_state.net_devices);
	net_dedup_state.num_net_devices = num_net_devices;

	// mmap shard cache into memory...
	int fd = shm_open(FINGERPRINT_CACHE_PATH, O_RDWR | O_CREAT | O_EXCL, 0660);
	// already exists
	if (fd == -1){
		while (fd == -1){
			// wait just in case the other instance didnt finish the initialization
			sleep(1);
			fd = shm_open(FINGERPRINT_CACHE_PATH, O_RDWR, 0);
		}
		printf("[Process %d] Found existing fingerprint cache in system, and mmapping it in to address space!\n", pid);
		net_dedup_state.global_fingerprint_cache = mmap(0,sizeof(Fingerprint_Cache),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	}
	// we just created it
	else{
		printf("[Process %d] Creating and initializing global fingerprint table & cache!\n\tTotal size (table + cache): %lu\n", pid, sizeof(Fingerprint_Cache));
		ftruncate(fd, sizeof(Fingerprint_Cache));
		net_dedup_state.global_fingerprint_cache = mmap(0,sizeof(Fingerprint_Cache),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);

		// intialize the cache correctly
		init_fingerprint_cache(net_dedup_state.global_fingerprint_cache);
	}


	return ncclSuccess;
}


// returns number of devices
ncclResult_t netDedup_devices(int * ndev) {
	
	*ndev = net_dedup_state.num_net_devices;
	return ncclSuccess;

}


// queries devices properties


// Following same pattern as: https://github.com/NVIDIA/nccl/blob/master/src/transport/net_socket.cc
ncclResult_t netDedup_getProperties(int dev, ncclNetProperties_v8_t * props) {
	
	if (dev >= net_dedup_state.num_net_devices){
		return ncclInvalidUsage;
	}

	Net_Socket_Dev q_dev = net_dedup_state.net_devices[dev];

	props -> name = q_dev.if_name;

	// this should only be for ib devices
	props -> guid = dev;
	// we could query /sysfs if needed to find this
	props -> pciPath = NULL;

	// using sockets means only host pointers
	props -> ptrSupport = NCCL_PTR_HOST;
	// registering memory will be seen across whole system becuase
	// we are using the fingerprint cache and shared memory

	// setting the same as in nccl-sockets as 0 for now...
	props -> regIsGlobal = 0;


	props -> speed = q_dev.if_speed;
	// netdev interface port number doesn't matter
	// default port numbers start at 1
	props -> port = 0;


	// will use nccl default
	props -> latency = 0;


	props -> maxComms = MAX_COMMS_NET_DEDUP_SOCKET_DEV;

	// ensure that we are not handling more than 1 receive at a time for now...
	props -> maxRecvs = 1;


	props->netDeviceType = NCCL_NET_DEVICE_HOST;
  	
	// not sure what this means...? API version? Something else for the proxy stuff..?
  	props->netDeviceVersion = NCCL_NET_DEVICE_INVALID_VERSION;


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