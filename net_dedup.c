#include "net_dedup.h"



// initializes network
ncclResult_t netDedup_init(ncclDebugLogger_t logFunction) {

	nccl_log_func = logFunction;
	
	pid_t pid = getpid();
	INFO(NCCL_NET | NCCL_INIT, "Initially loaded net dedup nccl plugin!\n", pid);

	int num_net_devices = init_net_socket_devs(net_dedup_state.net_devices);
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
		INFO(NCCL_NET | NCCL_INIT, "Found existing fingerprint cache in system, and mmapping it in to address space!\n", pid);
		net_dedup_state.global_fingerprint_cache = mmap(0,sizeof(Fingerprint_Cache),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	}
	// we just created it
	else{
		INFO(NCCL_NET | NCCL_INIT, "Creating and initializing global fingerprint table & cache!\n\tTotal size (table + cache): %lu\n", pid, sizeof(Fingerprint_Cache));
		ftruncate(fd, sizeof(Fingerprint_Cache));
		net_dedup_state.global_fingerprint_cache = mmap(0,sizeof(Fingerprint_Cache),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);

		// intialize the cache correctly
		init_fingerprint_cache(net_dedup_state.global_fingerprint_cache);
	}

	net_dedup_state.logFunction = logFunction;


	return ncclSuccess;
}


// returns number of devices
ncclResult_t netDedup_devices(int * ndev) {
	
	*ndev = net_dedup_state.num_net_devices;

	INFO(NCCL_NET | NCCL_INIT, "Found %d devices\n", net_dedup_state.num_net_devices);

	return ncclSuccess;

}


// queries devices properties


// Following same pattern as: https://github.com/NVIDIA/nccl/blob/master/src/transport/net_socket.cc
ncclResult_t netDedup_getProperties_v8(int dev, ncclNetProperties_v8_t * props) {

	INFO(NCCL_NET | NCCL_INIT, "Called getProperties() for device #%d\n", dev);
	
	if (dev >= net_dedup_state.num_net_devices){
		fprintf(stderr, "Error: calling get_properties on device %d, but only have %d net devices...\n", dev, net_dedup_state.num_net_devices);
		return ncclInvalidUsage;
	}

	Net_Socket_Dev q_dev = net_dedup_state.net_devices[dev];

	props -> name = q_dev.if_name;

	// this should only be for ib devices
	props -> guid = dev;
	// we could query /sysfs if needed to find this
	props -> pciPath = q_dev.pciPath;

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

	// not sure what this means...? API version? Something else for the proxy stuff..?
	props->netDeviceType = NCCL_NET_DEVICE_HOST;
  	props->netDeviceVersion = 8;


  	return ncclSuccess;
}

ncclResult_t netDedup_getProperties_v7(int dev, ncclNetProperties_v7_t * props) {
	
	INFO(NCCL_NET | NCCL_INIT, "Called getProperties() for device #%d\n", dev);
	
	if (dev >= net_dedup_state.num_net_devices){
		fprintf(stderr, "Error: calling get_properties on device %d, but only have %d net devices...\n", dev, net_dedup_state.num_net_devices);
		return ncclInvalidUsage;
	}

	Net_Socket_Dev q_dev = net_dedup_state.net_devices[dev];

	props -> name = q_dev.if_name;

	// this should only be for ib devices
	props -> guid = dev;
	// we could query /sysfs if needed to find this
	props -> pciPath = q_dev.pciPath;

	// using sockets means only host pointers
	props -> ptrSupport = NCCL_PTR_HOST;

	props -> speed = q_dev.if_speed;
	// netdev interface port number doesn't matter
	// default port numbers start at 1
	props -> port = 0;


	// will use nccl default
	props -> latency = 0;


	props -> maxComms = MAX_COMMS_NET_DEDUP_SOCKET_DEV;

	// ensure that we are not handling more than 1 receive at a time for now...
	props -> maxRecvs = 1;

	// not sure what this means...? API version? Something else for the proxy stuff..?
	props->netDeviceType = NCCL_NET_DEVICE_HOST;
  	props->netDeviceVersion = 7;

  	return ncclSuccess;
}


ncclResult_t netDedup_listen(int dev, void * handle, void ** listenComm) {

	printf("Called listen() from device %d\n", dev);

	return ncclInvalidUsage;
}

ncclResult_t netDedup_connect_v8(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v8_t** sendDevComm) {

	printf("Called connect() from device %d\n", dev);

	return ncclInvalidUsage;
}

ncclResult_t netDedup_connect_v7(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v7_t** sendDevComm) {

	

	return netDedup_connect_v8(dev, handle, sendComm, (ncclNetDeviceHandle_v8_t**) sendDevComm);
}


ncclResult_t netDedup_accept_v8(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v8_t** recvDevComm) {

	printf("Called accept()\n");

	return ncclInvalidUsage;
}

ncclResult_t netDedup_accept_v7(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v7_t** recvDevComm) {
	return netDedup_accept_v8(listenComm, recvComm, (ncclNetDeviceHandle_v8_t**) recvDevComm);
}


ncclResult_t netDedup_regMr(void * comm, void * data, size_t size, int type, void ** mhandle) {
	return ncclInvalidUsage;
}

ncclResult_t netDedup_regMr_v7(void * comm, void * data, int size, int type, void ** mhandle) {
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

ncclResult_t netDedup_irecv(void * recvComm, int n, void ** data, int * sizes, int * tags, void ** mhandles, void ** request) {
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