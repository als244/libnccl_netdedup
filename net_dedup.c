#include "net_dedup.h"



// initializes network
ncclResult_t netDedup_init(ncclDebugLogger_t logFunction) {

	nccl_log_func = logFunction;
	
	pid_t pid = getpid();
	INFO(NCCL_NET | NCCL_INIT, "Initially loaded net dedup nccl plugin!\n", pid);

	int num_net_devices = init_net_socket_devs(net_dedup_state.net_devices);

	if (num_net_devices == -1){
		return ncclSystemError;
	}

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

	int ret;

	INFO(NCCL_NET | NCCL_INIT, "Calling listen on dev #%d!\n", dev);

	// 1.) Get address of this device
	Net_Socket_Dev q_dev = net_dedup_state.net_devices[dev];
	struct sockaddr_in * saddr = &(q_dev.sa);

	// 2.) Create listening socket
	// 		- needs to be non-blocking so the accepts() won't block
	int listenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (listenFd < 0){
		perror("socket()");
		return ncclSystemError;
	}

	// 3.) Set socket options
	int enable = 1;
	ret = setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &enable, sizeof(enable));
	if (ret){
		perror("setsockopt()");
		return ncclSystemError;
	}

	ret = setsockopt(listenFd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
	if (ret != 0){
		perror("setsockopt() to set TCP_NODELAY (listen)\n");
		return ncclSystemError;
	}

	ret = setsockopt(listenFd, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable));
	if (ret != 0){
		perror("setsockopt() to set SO_ZEROCOPY (listen)\n");
		return ncclSystemError;
	}

	// 4.) Bind to the address returned from getifaddrs()
	//		- using port 0, will get assigned a port
	ret = bind(listenFd, saddr, sizeof(struct sockaddr_in));
	if (ret){
		perror("bind()");
		return ncclSystemError;
	}

	// 5.) Get the details of assigned addr
	struct sockaddr_in bound_addr;
	socklen_t len = sizeof(struct sockaddr_in);

	ret = getsockname(listenFd, (struct sockaddr *)&bound_addr, &len); 
	if (ret){
		perror("getsockname()");
		return ncclSystemError;
	}


	// 6.) Call listen
	ret = listen(listenFd, SOCKET_LISTEN_BACKLOG);
	if (ret){
		perror("listen()");
		return ncclSystemError;
	}


	// Set address for connect handle, so other side can call connect()
	Dedup_Connect_Handle * connect_handle = (Dedup_Connect_Handle *) handle;
	memset(connect_handle, 0, sizeof(Dedup_Connect_Handle));

	char *ip_addr = inet_ntoa(bound_addr.sin_addr);
	unsigned short port = ntohs(bound_addr.sin_port);
	INFO(NCCL_NET | NCCL_INIT, "Setting connect handle saddr to reference this listen fd:\n\tIP addr: %s\n\tPort: %u\n", ip_addr, port);

	memcpy(&(connect_handle -> addr), &bound_addr, sizeof(struct sockaddr_in));
	connect_handle -> in_progress = 0;
	connect_handle -> is_connected = 0;
	connect_handle -> connectingFd = -1;


	// Remember the file descriptor we are listening on so we can call accept
	Dedup_Listen_Comm * dedup_listen_comm = malloc(sizeof(Dedup_Listen_Comm));
	if (!dedup_listen_comm){
		perror("malloc() for listen_comm");
		return ncclSystemError;
	}

	dedup_listen_comm -> dev_num = dev;
	dedup_listen_comm -> listenFd = listenFd;
	dedup_listen_comm -> acceptedFd = -1;

	*listenComm = dedup_listen_comm;

	INFO(NCCL_NET | NCCL_INIT, "Successful listen for dev #%d!\n", dev);

	return ncclSuccess;
}


// ncclNetDeviceHandle_v8_t == ncclNetDeviceHandle_v7_t == ncclNetDeviceHandle_t
// within nccl_net_device.h
ncclResult_t netDedup_connect_v8(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v8_t** sendDevComm) {


	INFO(NCCL_NET | NCCL_INIT, "Calling connect() on dev #%d!\n", dev);

	Dedup_Connect_Handle * connect_handle = (Dedup_Connect_Handle *) handle;
	
	int ret;

	// assume we won't succeed
	*sendComm = NULL;

	// 1.) 
	if (connect_handle -> is_connected){

		INFO(NCCL_NET | NCCL_INIT, "Detected completed connect() for dev #%d, using fd #%d!\n", dev, connect_handle -> connectingFd);

		char is_ready = 1;

		ssize_t sent_bytes = send(connect_handle -> connectingFd, &is_ready, 1, 0);
			
		// for now just assume the send will go through, but really should have another state here...
		if (sent_bytes == -1){

			// will try to send again next round
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)){
				return ncclSuccess;
			}

			// otherwise something went wrong
			perror("send()");
			return ncclSystemError;
		}

		Dedup_Send_Comm * dedup_send_comm = malloc(sizeof(Dedup_Send_Comm));
		if (!dedup_send_comm){
			perror("malloc() for send_comm");
			return ncclSystemError;
		}

		dedup_send_comm -> dev_num = dev;
		dedup_send_comm -> fd = connect_handle -> connectingFd;
		memcpy(&(dedup_send_comm -> dest_addr), &(connect_handle -> addr), sizeof(struct sockaddr_in));

		
		// we are connected so set the send comm indicated the socket file descriptor to use
		*sendComm = dedup_send_comm;

		INFO(NCCL_NET | NCCL_INIT, "Successful connect() for dev #%d, using fd #%d!\n", dev, connect_handle -> connectingFd);

		return ncclSuccess;

	}

	// 2.) Determine if we already tried connecting, and if so check the status on the saved fd
	if (connect_handle -> in_progress){
		int progress_ret;
		socklen_t prog_ret_len = sizeof(int);
		ret = getsockopt(connect_handle -> connectingFd, SOL_SOCKET, SO_ERROR, (void*)&progress_ret, &prog_ret_len);
		if (ret){
			perror("getsockopt()");
			return ncclSystemError;
		}

		// we successfully connected
		// the next time around we will try to read the byte
		// that determines if we have been accepted by the other side
		if (progress_ret == 0){
			connect_handle -> is_connected = 1;
			return ncclSuccess;
		}
		else if (progress_ret == EINPROGRESS){
			return ncclSuccess;
		}
		else{
			fprintf(stderr, "Error: trying to connect failed after being in progress: %d\n", progress_ret);
			return ncclSystemError;
		}

	}

	
	// If we weren't in progress then create socket and call connect()

	// 1.) create connecting socket (must be non-blocking)
	int connectingFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (connectingFd < 0){
		perror("socket() for connect");
		return ncclSystemError;
	}

	connect_handle -> connectingFd = connectingFd;

	// 2.) Set socket options
	int enable = 1;
	ret = setsockopt(connect_handle -> connectingFd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
	if (ret){
		perror("setsockopt() to set TCP_NODELAY (connect)\n");
		return ncclSystemError;
	}

	ret = setsockopt(connect_handle -> connectingFd, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable));
	if (ret){
		perror("setsockopt() to set SO_ZEROCOPY (connect)\n");
		return ncclSystemError;
	}

	// 3.) Use the handle to where we are connecting to
	//		- this was created in listen and NCCL core sent this data out-of-band
	struct sockaddr_in saddr = connect_handle -> addr;

	char *ip_addr = inet_ntoa(saddr.sin_addr);
	unsigned short port = ntohs(saddr.sin_port);
	INFO(NCCL_NET | NCCL_INIT, "Prepreparing to connect to:\n\tIP addr: %s\n\tPort: %u\n", ip_addr, port);

	// 4.) call connect
	ret = connect(connectingFd, &saddr, sizeof(struct sockaddr_in));
	if (ret == -1){
		if (errno != EINPROGRESS){
			perror("connect() and errno not in progress");
			return ncclSystemError;
		}
		connect_handle -> in_progress = 1;
		return ncclSuccess;
	}

	// otherwise we have connected. next call to connect we will send
	connect_handle -> is_connected = 1;

	return ncclSuccess;
}

ncclResult_t netDedup_connect_v7(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v7_t** sendDevComm) {

	return netDedup_connect_v8(dev, handle, sendComm, (ncclNetDeviceHandle_v8_t**) sendDevComm);
}


ncclResult_t netDedup_accept_v8(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v8_t** recvDevComm) {

	// assume we will fail
	*recvComm = NULL;

	Dedup_Listen_Comm * dedup_listen_comm = (Dedup_Listen_Comm *) listenComm;

	int listenFd = dedup_listen_comm -> listenFd;

	INFO(NCCL_NET | NCCL_INIT, "Calling accept() on listenFd #%d!\n", listenFd);

	int acceptedFd;

	if (dedup_listen_comm -> acceptedFd != -1){

		INFO(NCCL_NET | NCCL_INIT, "Already accepted on listenFd #%d, with accepted fd #%d. Waiting to receive confirm!\n", listenFd, dedup_listen_comm -> acceptedFd);

		acceptedFd = dedup_listen_comm -> acceptedFd;

		// now need to send a byte on the this socket so the other side knows we have accepted
		char is_ready;
		ssize_t recv_bytes = recv(acceptedFd, &is_ready, 1, 0);

		// assume that the receviing 1 byte will be ready...
		if (recv_bytes == -1){

			if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
				return ncclSuccess;
			}

			perror("recv()");
			return ncclSystemError;
		}

		// we successfully received results and can now return!

		Dedup_Recv_Comm * dedup_recv_comm = malloc(sizeof(Dedup_Recv_Comm));
		if (!dedup_recv_comm){
			perror("malloc() for dedup_recv_comm");
			return ncclSystemError;
		} 

		memcpy(&(dedup_recv_comm -> src_addr), &(dedup_listen_comm -> src_addr), sizeof(struct sockaddr_in));
		dedup_recv_comm -> dev_num = dedup_listen_comm -> dev_num;
		dedup_recv_comm -> fd = acceptedFd;
		

		*recvComm = dedup_recv_comm;


		INFO(NCCL_NET | NCCL_INIT, "Successful accept() on listenFd #%d!\n\tAccepted Fd: %d\n", listenFd, acceptedFd);

		return ncclSuccess;
	}

	struct sockaddr_in remote_sockaddr;
	socklen_t remote_len = sizeof(remote_sockaddr);

	acceptedFd = accept4(listenFd, (struct sockaddr *) &remote_sockaddr, &remote_len, SOCK_NONBLOCK);

	if (acceptedFd == -1){
		// no one is trying to connect
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)){
			INFO(NCCL_NET | NCCL_INIT, "No accepts() ready for dev #%d, using listen fd #%d!\n", dedup_listen_comm -> dev_num, listenFd);
			return ncclSuccess;
		}
		else{
			// otherwise we will fail
			perror("accept()");
			return ncclSystemError;
		}
		
	}

	dedup_listen_comm -> acceptedFd = acceptedFd;
	memcpy(&(dedup_listen_comm -> src_addr), &remote_sockaddr, sizeof(struct sockaddr_in));
	
	// next iteration of accept() we will try to recv data from connecting side...

	return ncclSuccess;
}

ncclResult_t netDedup_accept_v7(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v7_t** recvDevComm) {
	return netDedup_accept_v8(listenComm, recvComm, (ncclNetDeviceHandle_v8_t**) recvDevComm);
}


ncclResult_t netDedup_regMr(void * comm, void * data, size_t size, int type, void ** mhandle) {

	INFO(NCCL_NET | NCCL_INIT, "Called regMr()\n");

	if (type != NCCL_PTR_HOST){
		return ncclInternalError;
	}

	return ncclSuccess;
}

ncclResult_t netDedup_regMr_v7(void * comm, void * data, int size, int type, void ** mhandle) {

	INFO(NCCL_NET | NCCL_INIT, "Called regMr()\n");

	if (type != NCCL_PTR_HOST){
		return ncclInternalError;
	}
	
	return ncclSuccess;
}


ncclResult_t netDedup_regMrDmaBuf(void* comm, void* data, size_t size, int type, uint64_t offset, int fd, void** mhandle) {

	INFO(NCCL_NET | NCCL_INIT, "Called regMrDmaBuf()\n");
	
	INFO(NCCL_NET | NCCL_INIT, "Called regMr()\n");

	if (type != NCCL_PTR_HOST){
		return ncclInternalError;
	}
	
	return ncclSuccess;
}

ncclResult_t netDedup_deregMr(void * comm, void * mhandle) {

	INFO(NCCL_NET | NCCL_INIT, "Called deregMr()\n");
	return ncclSuccess;
}

ncclResult_t netDedup_isend(void * sendComm, void * data, int size, int tag, void * mhandle, void ** request) {

	Dedup_Send_Comm * dedup_send_comm = (Dedup_Send_Comm *) sendComm;
	int dev_num = dedup_send_comm -> dev_num;

	INFO(NCCL_NET | NCCL_INIT, "Calling isend() on dev #%d!\n\tSize: %d", dev_num, size);


	return ncclInvalidUsage;
}

ncclResult_t netDedup_irecv(void * recvComm, int n, void ** data, int * sizes, int * tags, void ** mhandles, void ** request) {

	Dedup_Recv_Comm * dedup_recv_comm = (Dedup_Recv_Comm *) recvComm;
	int dev_num = dedup_recv_comm -> dev_num;

	INFO(NCCL_NET | NCCL_INIT, "Calling irecv() on dev #%d!\n\tSize: %d", dev_num, sizes[0]);

	return ncclInvalidUsage;
}

ncclResult_t netDedup_iflush(void * recvComm, int n, void ** data, int * sizes, void ** mhandles, void ** request) {
	
	INFO(NCCL_NET | NCCL_INIT, "Called iflush()\n");

	return ncclInvalidUsage;
}

ncclResult_t netDedup_test(void * request, int * done, int * sizes) {

	INFO(NCCL_NET | NCCL_INIT, "Called test()\n");

	return ncclInvalidUsage;
}


ncclResult_t netDedup_closeSend(void * sendComm) {

	Dedup_Send_Comm * dedup_send_comm = (Dedup_Send_Comm *) sendComm;

	INFO(NCCL_NET | NCCL_INIT, "Called closeSend() for fd: %d\n", dedup_send_comm -> fd);

	free(dedup_send_comm);

	return ncclSuccess;
}

ncclResult_t netDedup_closeRecv(void * recvComm) {

	Dedup_Recv_Comm * dedup_recv_comm = (Dedup_Recv_Comm *) recvComm;

	INFO(NCCL_NET | NCCL_INIT, "Called closeRecv() for fd: %d\n", dedup_recv_comm -> fd);

	free(dedup_recv_comm);

	return ncclSuccess;
}


ncclResult_t netDedup_closeListen(void * listenComm) {

	Dedup_Listen_Comm * dedup_listen_comm = (Dedup_Listen_Comm *) listenComm;
	int listenFd = dedup_listen_comm -> listenFd;

	INFO(NCCL_NET | NCCL_INIT, "Called closeListen() for listenFd: %d\n", listenFd);

	free(dedup_listen_comm);

	return ncclSuccess;
}


ncclResult_t netDedup_getDeviceMr(void * comm, void * mhandle, void ** dptr_mhandle) {

	INFO(NCCL_NET | NCCL_INIT, "Called getDeviceMr()\n");

	return ncclInvalidUsage;
}


ncclResult_t netDedup_irecvConsumed(void * recvComm, int n, void * request) {

	INFO(NCCL_NET | NCCL_INIT, "Called irecvConsumed()\n");

	return ncclInvalidUsage;
}