#include "net_dedup.h"



// initializes network
ncclResult_t netDedup_init(ncclDebugLogger_t logFunction) {

	nccl_log_func = logFunction;
	

	if (TO_LOG_NCCL_API_INIT){
		INFO(NCCL_NET | NCCL_INIT, "Initially loaded net dedup nccl plugin!\n");
	}

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
		if (TO_LOG_NCCL_API_INIT){
			INFO(NCCL_NET | NCCL_INIT, "Found existing fingerprint cache in system, and mmapping it in to address space!\n");
		}

		global_fingerprint_cache = mmap(0,sizeof(Fingerprint_Cache),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
		if (!global_fingerprint_cache){
			WARN("ERROR: Unable to map global_fingerprint_cache into process. Return ncclSystemError...\n");
			return ncclSystemError;
		}
	}
	// we just created it
	else{
		
		if (TO_LOG_NCCL_API_INIT){
			INFO(NCCL_NET | NCCL_INIT, "Creating and initializing global fingerprint table & cache!\n\tTotal size (table + cache): %lu\n", sizeof(Fingerprint_Cache));
		}
		ftruncate(fd, sizeof(Fingerprint_Cache));
		global_fingerprint_cache = mmap(0,sizeof(Fingerprint_Cache),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
		if (!global_fingerprint_cache){
			WARN("ERROR: Unable to map global_fingerprint_cache into process. Return ncclSystemError...\n");
			return ncclSystemError;
		}

		// intialize the cache correctly
		init_fingerprint_cache(global_fingerprint_cache);
	}

	net_dedup_state.logFunction = logFunction;

	for (int i = 0; i < MAX_FDS; i++){
		active_fds[i] = 0;
	}

		
	to_skip_insert_cache = 0;

	char * to_skip_env = getenv("SKIP_INSERT_CACHE");
	if (to_skip_env && (strncmp(to_skip_env, "1", 1) == 0)){
		to_skip_insert_cache = 1;
	}


	return ncclSuccess;
}


// returns number of devices
ncclResult_t netDedup_devices(int * ndev) {
	
	*ndev = net_dedup_state.num_net_devices;

	if (TO_LOG_NCCL_API_INIT){
		INFO(NCCL_NET | NCCL_INIT, "Found %d devices\n", net_dedup_state.num_net_devices);
	}

	return ncclSuccess;

}


// queries devices properties


// Following same pattern as: https://github.com/NVIDIA/nccl/blob/master/src/transport/net_socket.cc
ncclResult_t netDedup_getProperties_v8(int dev, ncclNetProperties_v8_t * props) {
	
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

	if (TO_LOG_NCCL_API_CONN_ESTABLISH){
		INFO(NCCL_NET | NCCL_INIT, "Calling listen on dev #%d!\n", dev);
	}

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
	
	if (TO_LOG_NCCL_API_CONN_ESTABLISH){
		INFO(NCCL_NET | NCCL_INIT, "Setting connect handle saddr to reference this listen fd:\n\tIP addr: %s\n\tPort: %u\n", ip_addr, port);
	}

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

	if (TO_LOG_NCCL_API_CONN_ESTABLISH){
		INFO(NCCL_NET | NCCL_INIT, "Successful listen for dev #%d!\n", dev);
	}

	return ncclSuccess;
}


// ncclNetDeviceHandle_v8_t == ncclNetDeviceHandle_v7_t == ncclNetDeviceHandle_t
// within nccl_net_device.h
ncclResult_t netDedup_connect_v8(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v8_t** sendDevComm) {

	if (TO_LOG_NCCL_API_CONN_ESTABLISH){
		INFO(NCCL_NET | NCCL_INIT, "Calling connect() on dev #%d!\n", dev);
	}

	Dedup_Connect_Handle * connect_handle = (Dedup_Connect_Handle *) handle;
	
	int ret;

	// assume we won't succeed
	*sendComm = NULL;

	// 1.) 
	if (connect_handle -> is_connected){

		if (TO_LOG_NCCL_API_CONN_ESTABLISH){
			INFO(NCCL_NET | NCCL_INIT, "Detected completed connect() for dev #%d, using fd #%d!\n", dev, connect_handle -> connectingFd);
		}

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

		if (TO_LOG_NCCL_API_CONN_ESTABLISH){
			INFO(NCCL_NET | NCCL_INIT, "Successful connect() for dev #%d, using fd #%d!\n", dev, connect_handle -> connectingFd);
		}

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
	
	if (TO_LOG_NCCL_API_CONN_ESTABLISH){
		INFO(NCCL_NET | NCCL_INIT, "Prepreparing to connect to:\n\tIP addr: %s\n\tPort: %u\n", ip_addr, port);
	}

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

	if (TO_LOG_NCCL_API_CONN_ESTABLISH){
		INFO(NCCL_NET | NCCL_INIT, "Calling accept() on listenFd #%d!\n", listenFd);
	}

	int acceptedFd;

	if (dedup_listen_comm -> acceptedFd != -1){

		if (TO_LOG_NCCL_API_CONN_ESTABLISH){
			INFO(NCCL_NET | NCCL_INIT, "Already accepted on listenFd #%d, with accepted fd #%d. Waiting to receive confirm!\n", listenFd, dedup_listen_comm -> acceptedFd);
		}

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

		if (TO_LOG_NCCL_API_CONN_ESTABLISH){
			INFO(NCCL_NET | NCCL_INIT, "Successful accept() on listenFd #%d!\n\tAccepted Fd: %d\n", listenFd, acceptedFd);
		}

		return ncclSuccess;
	}

	struct sockaddr_in remote_sockaddr;
	socklen_t remote_len = sizeof(remote_sockaddr);

	acceptedFd = accept4(listenFd, (struct sockaddr *) &remote_sockaddr, &remote_len, SOCK_NONBLOCK);

	if (acceptedFd == -1){
		// no one is trying to connect
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)){
			
			if (TO_LOG_NCCL_API_CONN_ESTABLISH){
				INFO(NCCL_NET | NCCL_INIT, "No accepts() ready for dev #%d, using listen fd #%d!\n", dedup_listen_comm -> dev_num, listenFd);
			}

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


ncclResult_t netDedup_closeListen(void * listenComm) {

	Dedup_Listen_Comm * dedup_listen_comm = (Dedup_Listen_Comm *) listenComm;
	
	if (!dedup_listen_comm){
		return ncclSuccess;
	}	

	int listenFd = dedup_listen_comm -> listenFd;

	if (TO_LOG_NCCL_API_CONN_ESTABLISH){
		INFO(NCCL_NET | NCCL_INIT, "Called closeListen() for listenFd: %d\n", listenFd);
	}

	close(listenFd);
	free(dedup_listen_comm);

	return ncclSuccess;
}


// Following implementation from https://github.com/NVIDIA/nccl/blob/master/src/transport/net_socket.cc
//	regarding what errors to return

ncclResult_t netDedup_regMr_v8(void * comm, void * data, size_t size, int type, void ** mhandle) {

	if (TO_LOG_NCCL_API_RDMA_MR){
		INFO(NCCL_NET | NCCL_INIT, "Called regMr()\n");
	}

	if (type != NCCL_PTR_HOST){
		return ncclInternalError;
	}

	return ncclSuccess;
}

ncclResult_t netDedup_regMr_v7(void * comm, void * data, int size, int type, void ** mhandle) {

	if (TO_LOG_NCCL_API_RDMA_MR){
		INFO(NCCL_NET | NCCL_INIT, "Called regMr()\n");
	}

	if (type != NCCL_PTR_HOST){
		return ncclInternalError;
	}
	
	return ncclSuccess;
}


ncclResult_t netDedup_regMrDmaBuf(void* comm, void* data, size_t size, int type, uint64_t offset, int fd, void** mhandle) {

	if (TO_LOG_NCCL_API_RDMA_MR){
		INFO(NCCL_NET | NCCL_INIT, "Called regMrDmaBuf()\n");
	}

	if (type != NCCL_PTR_HOST){
		return ncclInternalError;
	}
	
	return ncclSuccess;
}

ncclResult_t netDedup_deregMr(void * comm, void * mhandle) {

	if (TO_LOG_NCCL_API_RDMA_MR){
		INFO(NCCL_NET | NCCL_INIT, "Called deregMr()\n");
	}

	return ncclSuccess;
}


ncclResult_t netDedup_getDeviceMr(void * comm, void * mhandle, void ** dptr_mhandle) {

	if (TO_LOG_NCCL_API_RDMA_MR){
		INFO(NCCL_NET | NCCL_INIT, "Called getDeviceMr()\n");
	}

	return ncclInternalError;
}

ncclResult_t netDedup_iflush(void * recvComm, int n, void ** data, int * sizes, void ** mhandles, void ** request) {
	
	if (TO_LOG_NCCL_API_FLUSH){
		INFO(NCCL_NET | NCCL_INIT, "Called iflush()\n");
	}
	return ncclInternalError;
}



/* DEALING WITH SEND() PROTOCOL! */

int process_send_header(Dedup_Send_Req * send_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: send_header()\n");
	}

	int sockfd = send_req -> sockfd;

	int prev_sent = send_req -> send_header_offset;
	void * cur_header = &(send_req -> header) + prev_sent;
	size_t remain_size = sizeof(General_Header) - prev_sent;

	ssize_t sent_bytes = send(sockfd, cur_header, remain_size, 0);
	if (sent_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("send() during header send");
		return -1;
	}

	if (sent_bytes < remain_size){
		send_req -> send_header_offset += sent_bytes;
		return 0;
	}

	// otherwise we read the whole header
	if (TO_LOG_GENERAL_HEADERS){
		char is_fingerprint = send_req -> header.is_fingerprint;
		uint64_t num_bytes = send_req -> header.content_size;
		INFO(NCCL_NET | NCCL_INIT, "Sent General Header:\n\t\t\t\tSockfd: %d\n\t\t\t\tIs fingerprint: %d\n\t\t\t\tContent Size: %lu\n\n", sockfd, (int) is_fingerprint, num_bytes);
	}

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: send_header()\n");
	}

	return 1;

}

int process_send_reg_data(Dedup_Send_Req * send_req) {

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: send_reg_data()\n");
	}

	int sockfd = send_req -> sockfd;


	void * data = send_req -> data;
	uint64_t size = send_req -> size;
	uint64_t offset = send_req -> offset;

	void * cur_data = data + offset;
	uint64_t remain_bytes = size - offset;

	if (remain_bytes == 0){
		return 1;
	}

	ssize_t sent_bytes = send(sockfd, cur_data, remain_bytes, 0);

	if (sent_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("send() during reg data send");
		return -1;
	}

	send_req -> offset += sent_bytes;

	
	// if there are some bytes remaining don't indicate to continue to next stage
	if (send_req -> offset < size){
		return 0;
	}

	// otherwise finished sending the whole thing
	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: send_reg_data()\n");
	}
	
	return 1;

}

uint64_t dedup_fingerprinting(int sockfd, void * data, size_t n, Fingerprint ** ret_packaged_fingerprints){

	Fingerprinting_Settings * settings = &((global_fingerprint_cache) -> fingerprinting_settings);
	uint64_t max_fingerprints = (n / (settings -> min_chunk_size_bytes)) + 1;
	uint64_t num_fingerprints;
	uint8_t * raw_fingerprint_buffer = malloc(max_fingerprints * FINGERPRINT_NUM_BYTES);
	uint64_t * content_sizes = malloc(max_fingerprints * sizeof(uint64_t));

	do_fingerprinting((uint8_t *) data, n, &num_fingerprints, raw_fingerprint_buffer, content_sizes,
		settings -> rabin_p, settings -> rabin_m_bits, settings -> rabin_table, settings -> window_bytes, settings -> lower_bits, settings -> min_chunk_size_bytes, settings -> max_chunk_size_bytes, settings -> magic_val);

	if (TO_LOG_FINGERPRINT_COMPUTATION){
		INFO(NCCL_NET | NCCL_INIT, "Computed fingerprints\n\t\t\t\tSockfd: %d\n\t\t\t\tBuffer Size: %llu\n\t\t\t\t# Fingerprints: %llu\n\n", sockfd, n, num_fingerprints);
	}

	Fingerprint * packaged_fingerprints = malloc(num_fingerprints * sizeof(Fingerprint));

	for (uint64_t i = 0; i < num_fingerprints; i++){
		memcpy(packaged_fingerprints[i].fingerprint, &(raw_fingerprint_buffer[i * FINGERPRINT_NUM_BYTES]), FINGERPRINT_NUM_BYTES);
		packaged_fingerprints[i].content_size = content_sizes[i];
	}

	free(content_sizes);
	free(raw_fingerprint_buffer);

	*ret_packaged_fingerprints = packaged_fingerprints;

	return num_fingerprints;

}


int process_compute_fingerprints(Dedup_Send_Req * send_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: compute_fingerprints()\n");
	}

	// 1.) compute all the fingerprints

	int sockfd = send_req -> sockfd;

	void * data = send_req -> data;
	uint64_t size = send_req -> size;

	Fingerprint_Send_State * send_state = &(send_req -> send_fingerprint_state);
	Fingerprint_Header * fingerprint_header = &(send_req -> fingerprint_header);

	uint64_t num_fingerprints = dedup_fingerprinting(sockfd, data, size, &(send_state -> packaged_fingerprints));
	fingerprint_header -> num_fingerprints = num_fingerprints;

	uint64_t * fingerprint_offsets = malloc(num_fingerprints * sizeof(uint64_t));
	if (!fingerprint_offsets){
		perror("malloc() for fingerprint_offsets");
		return -1;
	}

	Fingerprint * packaged_fingerprints = send_state -> packaged_fingerprints;
	uint64_t cur_offset = 0;
	for (uint64_t i = 0; i < num_fingerprints; i++){
		fingerprint_offsets[i] = cur_offset;
		cur_offset += packaged_fingerprints[i].content_size;
		if (TO_PRINT_FINGERPRINT_INFO){
			printf("Computed Fingerprint #%lu\n\tContent Size: %lu\n\tHex: ", i, packaged_fingerprints[i].content_size);
			print_sha256(packaged_fingerprints[i].fingerprint);
			printf("\n");
		}
	}

	send_state -> fingerprint_offsets = fingerprint_offsets;

	// 3.) initialize the structures needed for this send request

	// 3a.) sending fingprints
	send_state -> send_fingerprint_offset = 0;
	send_state -> packaged_fingerprints_size_bytes = num_fingerprints * sizeof(Fingerprint);

	// 3b.) receiving missing fingerprints
	send_state -> missing_fingerprint_header.num_missing_fingerprints = 0;
	send_state -> missing_fingerprint_inds = malloc(num_fingerprints * sizeof(uint64_t));
	if (!(send_state -> missing_fingerprint_inds)){
		perror("malloc() for preparing missing fingerprint inds buffer");
		return -1;
	}
	send_state -> missing_fingerprint_header_offset = 0;
	send_state -> recv_missing_fingerprint_inds_offset = 0;
	
	// 3c.) repalying with content
	send_state -> cur_reply_content_fingerprint_ind = 0;
	send_state -> cur_reply_content_fingerprint_offset = 0;

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: compute_fingerprints()\n");
	}

	return 1;
}

int my_breakpoint_func(uint64_t num_fingerprints){

	return num_fingerprints >> 60;

}

int process_insert_outbound_fingerprints(Dedup_Send_Req * send_req){

	

	if (to_skip_insert_cache){
		return 1;
	}

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: insert_outbound_fingerprints()\n");
	}

	// 1.) try to obtain cache lock
	if (pthread_mutex_trylock(&(global_fingerprint_cache -> cache_lock)) != 0){
		return 0;
	}

	// we obtained the lock, so continue

	// 2.) insert all the fingerprints into local cache to retrieve content refs

	uint64_t num_fingerprints = send_req -> fingerprint_header.num_fingerprints;

	Fingerprint * packaged_fingerprints = send_req -> send_fingerprint_state.packaged_fingerprints;

	int ret;

	void * data = send_req -> data;
	void * cur_buffer = data;
	Fingerprint_Entry new_entry;
	for (uint64_t i = 0; i < num_fingerprints; i++){
		// takes care of duplicates
		// we are saving the content refs that might be needed for reply without cache lookup again
		ret = insert_fingerprint(global_fingerprint_cache, &(packaged_fingerprints[i]), cur_buffer, &new_entry);
		if (ret){
			fprintf(stderr, "CACHE IS FULL\n");
			pthread_mutex_unlock(&(global_fingerprint_cache -> cache_lock));
			to_skip_insert_cache = 1;
			if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
				INFO(NCCL_NET | NCCL_INIT, "Completed: insert_outbound_fingerprints()\n");
			}
			return 1;
		}
		cur_buffer += packaged_fingerprints[i].content_size;
	}

	pthread_mutex_unlock(&(global_fingerprint_cache -> cache_lock));

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: insert_outbound_fingerprints()\n");
	}	

	return 1;
}

int process_send_fingerprint_header(Dedup_Send_Req * send_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: send_fingerprint_header()\n");
	}

	int sockfd = send_req -> sockfd;

	int prev_sent = send_req -> send_fingerprint_header_offset;
	void * cur_header = &(send_req -> fingerprint_header) + prev_sent;
	size_t remain_size = sizeof(Fingerprint_Header) - prev_sent;

	ssize_t sent_bytes = send(sockfd, cur_header, remain_size, 0);
	if (sent_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("send() during fingerprint header send");
		return -1;
	}	

	// if we didn't finish sending the header
	if (sent_bytes < remain_size){
		send_req -> send_fingerprint_header_offset += sent_bytes;
		return 0;
	}

	// otherwise we sent the entire header, so we can continue
	if (TO_LOG_FINGERPRINT_HEADERS){
		uint64_t num_fingerprints = send_req -> fingerprint_header.num_fingerprints;
		INFO(NCCL_NET | NCCL_INIT, "Sent Fingerprint Header:\n\t\t\t\tSockfd: %d\n\t\t\t\tNum fingerprints: %lu\n\n", sockfd, num_fingerprints);
	}

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: send_fingerprint_header()\n");
	}

	return 1;
}

int process_send_packaged_fingerprints(Dedup_Send_Req * send_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: send_packaged_fingerprints()\n");
	}

	int sockfd = send_req -> sockfd;

	int prev_sent = send_req -> send_fingerprint_state.send_fingerprint_offset;

	void * cur_packaged_fingerprints = ((void *) send_req -> send_fingerprint_state.packaged_fingerprints) + prev_sent;

	uint64_t packaged_fingerprints_size_bytes = send_req -> send_fingerprint_state.packaged_fingerprints_size_bytes;

	size_t remain_size = packaged_fingerprints_size_bytes - prev_sent;

	ssize_t sent_bytes = send(sockfd, cur_packaged_fingerprints, remain_size, 0);
	if (sent_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("send() during packaged fingerprints");
		return -1;
	}

	if (sent_bytes < remain_size){
		send_req -> send_fingerprint_state.send_fingerprint_offset += sent_bytes;
		return 0;
	}

	// otherwise we sent the whole thing so we can continue

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: send_packaged_fingerprints()\n");
	}

	return 1;
}

int process_recv_missing_fingerprint_header(Dedup_Send_Req * send_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: recv_missing_fingerprint_header()\n");
	}

	int sockfd = send_req -> sockfd;

	int prev_recv = send_req -> send_fingerprint_state.missing_fingerprint_header_offset;

	void * cur_header = ((void *) &(send_req -> send_fingerprint_state.missing_fingerprint_header)) + prev_recv;

	size_t remain_size = sizeof(Missing_Fingerprint_Header) - prev_recv;

	ssize_t recv_bytes = recv(sockfd, cur_header, remain_size, 0);

	if (recv_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("recv() during recv missing fingerprints header");
		return -1;
	}


	if (recv_bytes < remain_size){
		send_req -> send_fingerprint_state.missing_fingerprint_header_offset += recv_bytes;
		return 0;
	}

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: recv_missing_fingerprint_header()\n");
	}

	return 1;

}

int process_recv_missing_fingerprints(Dedup_Send_Req * send_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: recv_missing_fingerprints()\n");
	}

	int sockfd = send_req -> sockfd;

	uint64_t num_missing_fingerprints = send_req -> send_fingerprint_state.missing_fingerprint_header.num_missing_fingerprints;

	uint64_t total_size = num_missing_fingerprints * sizeof(uint64_t);

	uint64_t recv_missing_fingerprint_inds_offset = send_req -> send_fingerprint_state.recv_missing_fingerprint_inds_offset;

	uint64_t * missing_fingerprint_inds = send_req -> send_fingerprint_state.missing_fingerprint_inds;

	void * cur_missing_fingerprints = ((void *) missing_fingerprint_inds) + recv_missing_fingerprint_inds_offset;
	
	uint64_t remain_size = total_size - recv_missing_fingerprint_inds_offset;

	ssize_t recv_bytes = recv(sockfd, cur_missing_fingerprints, remain_size, 0);

	if (recv_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("recv() during recv missing fingerprints");
		return -1;
	}

	if (recv_bytes < remain_size){
		send_req -> send_fingerprint_state.recv_missing_fingerprint_inds_offset += recv_bytes;
		return 0;
	}

	// otherwise we have received all the missing fingerprint inds
	
	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: recv_missing_fingerprints()\n");
	}

	return 1;
}

int process_send_missing_content(Dedup_Send_Req * send_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: send_missing_content()\n");
	}

	int sockfd = send_req -> sockfd;

	Fingerprint * packaged_fingerprints = (send_req -> send_fingerprint_state).packaged_fingerprints;
	uint64_t num_missing_fingerprints = (send_req -> send_fingerprint_state).missing_fingerprint_header.num_missing_fingerprints;
	uint64_t * fingerprint_offsets = (send_req -> send_fingerprint_state).fingerprint_offsets;
	uint64_t * missing_fingerprint_inds = (send_req -> send_fingerprint_state).missing_fingerprint_inds;

	uint64_t reply_ind;

	ssize_t sent_bytes;

	// 1.) send as many more fingerprints as possible
	uint64_t cur_send_fingerprint_ind = (send_req -> send_fingerprint_state).cur_reply_content_fingerprint_ind;
	uint64_t cur_fingerprint_offset = (send_req -> send_fingerprint_state).cur_reply_content_fingerprint_offset;

	uint64_t remain_bytes;

	void * cur_fingerprint;
	
	for (uint64_t i = cur_send_fingerprint_ind; i < num_missing_fingerprints; i++){

		reply_ind = missing_fingerprint_inds[i];


		cur_fingerprint = (send_req -> data) + fingerprint_offsets[reply_ind] + cur_fingerprint_offset;

		// in the case of first fingerprint in this loop in case we couldn't send the whole thing the last time
		// otherwise cur_offset will be set to 0
		remain_bytes = packaged_fingerprints[reply_ind].content_size - cur_fingerprint_offset;
		
		sent_bytes = send(sockfd, cur_fingerprint, remain_bytes, 0);

		if (sent_bytes == -1){
			if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
				return 0;
			}
			perror("send() during content reply");
			return -1;
		}

		if (sent_bytes < remain_bytes){
			(send_req -> send_fingerprint_state).cur_reply_content_fingerprint_ind = i;
			(send_req -> send_fingerprint_state).cur_reply_content_fingerprint_offset = cur_fingerprint_offset + sent_bytes;
			return 0;
		}

		// otherwise we sent the whole fingerprint so update for next iter
		(send_req -> send_fingerprint_state).cur_reply_content_fingerprint_ind = i + 1;
		(send_req -> send_fingerprint_state).cur_reply_content_fingerprint_offset = 0;
	
		cur_fingerprint_offset = 0;
	}

	// if we complete this loop then we are done
	
	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: send_missing_content()\n");
	}

	return 1;
}

void process_send_complete(Dedup_Send_Req * send_req){
	// if this was a fingerprint send need to free resources and return 1
	return;
}


int process_send(Dedup_Send_Req * send_req){

	int to_continue = 1;
	while (to_continue){
		switch (send_req -> stage){
			case SEND_HEADER:
				to_continue = process_send_header(send_req);
				if (to_continue == 1){
					if (send_req -> header.is_fingerprint){
						send_req -> stage = COMPUTE_FINGERPRINTS;
					}
					else{
						send_req -> stage = SEND_REG_DATA;
					}
				}
				break;
			case SEND_REG_DATA:
				to_continue = process_send_reg_data(send_req);
				if (to_continue == 1){
					send_req -> stage = SEND_COMPLETE;
				}
				break;
			case COMPUTE_FINGERPRINTS:
				to_continue = process_compute_fingerprints(send_req);
				if (to_continue == 1){
					send_req -> stage = INSERT_OUTBOUND_FINGERPRINTS;
				}
				break;
			case INSERT_OUTBOUND_FINGERPRINTS:
				to_continue = process_insert_outbound_fingerprints(send_req);
				if (to_continue == 1){
					send_req -> stage = SEND_FINGERPRINT_HEADER;
				}
				break;
			case SEND_FINGERPRINT_HEADER:
				to_continue = process_send_fingerprint_header(send_req);
				if (to_continue == 1){
					send_req -> stage = SEND_PACKAGED_FINGERPRINTS;
				}
				break;
			case SEND_PACKAGED_FINGERPRINTS:
				to_continue = process_send_packaged_fingerprints(send_req);
				if (to_continue == 1){
					send_req -> stage = RECV_MISSING_FINGERPRINT_HEADER;
				}
				break;
			case RECV_MISSING_FINGERPRINT_HEADER:
				to_continue = process_recv_missing_fingerprint_header(send_req);
				if (to_continue == 1){
					if ((send_req -> send_fingerprint_state).missing_fingerprint_header.num_missing_fingerprints == 0){
						send_req -> stage = SEND_COMPLETE;
					}
					else{
						send_req -> stage = RECV_MISSING_FINGERPRINTS;
					}
				}
				break;
			case RECV_MISSING_FINGERPRINTS:
				to_continue = process_recv_missing_fingerprints(send_req);
				if (to_continue == 1){
					send_req -> stage = SEND_MISSING_CONTENT;
				}
				break;
			case SEND_MISSING_CONTENT:
				to_continue = process_send_missing_content(send_req);
				if (to_continue == 1){
					send_req -> stage = SEND_COMPLETE;
				}
				break;
			case SEND_COMPLETE:
				process_send_complete(send_req);
				return 1;
			default:
				fprintf(stderr, "Error: unknown send request stage: %d...\n", send_req -> stage);
				return -1;
		}
		if (to_continue == -1){
			fprintf(stderr, "Error: unable to process send during stage: %d\n", send_req -> stage);
			return -1;
		}
	}
	// if we made it out here then we didn't return from SEND_COMPLETE and also didn't have error, so return 0
	return 0;
}

ncclResult_t netDedup_isend(void * sendComm, void * data, int size, int tag, void * mhandle, void ** request) {

	int ret;

	Dedup_Send_Comm * dedup_send_comm = (Dedup_Send_Comm *) sendComm;
	int dev_num = dedup_send_comm -> dev_num;
	int sockfd = dedup_send_comm -> fd;

	if (TO_LOG_NCCL_API_SENDS){
		INFO(NCCL_NET | NCCL_INIT, "Calling isend():\n\tDev: %d\n\tFd: %d\n\tSize: %d", dev_num, sockfd, size);
	}

	if (active_fds[sockfd]){
		*request = NULL;
		return ncclSuccess;
	}

	Dedup_Req * req = malloc(sizeof(Dedup_Req));
	if (!req){
		perror("malloc() for req for send");
		return ncclSystemError;
	}

	req -> type = SEND_REQ;

	Dedup_Send_Req * send_req = malloc(sizeof(Dedup_Send_Req));
	if (!send_req){
		perror("malloc() for send_req");
		return ncclSystemError;
	}

	memset(send_req, 0, sizeof(Dedup_Send_Req));

	send_req -> sockfd = dedup_send_comm -> fd;
	send_req -> size = size;
	send_req -> data = data;
	send_req -> offset = 0;
	send_req -> send_header_offset = 0;
	send_req -> send_fingerprint_header_offset = 0;


	if (size > FINGERPRINT_MSG_SIZE_THRESHOLD){
		send_req -> header.is_fingerprint = 1;
	}
	else{
		send_req -> header.is_fingerprint = 0;
	}
	send_req -> header.content_size = size;

	send_req -> stage = SEND_HEADER;

	// process as much as we can
	// send_req state will be updated for as far as we get
	// it will continue to progress every time "test" is called
	// (could have seperate threads that process asynchronously)
	ret = process_send(send_req);

	// if there was an error we need to report it
	if (ret == -1){
		fprintf(stderr, "Error: had an issue when processing send\n");
		return ncclSystemError;
	}

	// the test function will check if this has completed

	// ensure to save the request
	req -> req = send_req;
	*request = req;

	active_fds[sockfd] = 1;


	return ncclSuccess;
}


/* DEALING WITH RECV() PROTOCOL! */

int process_recv_header(Dedup_Recv_Req * recv_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: recv_header()\n");
	}

	int sockfd = recv_req -> sockfd;

	int prev_recv = recv_req -> recv_header_offset;
	void * cur_header = &(recv_req -> header) + prev_recv;
	size_t remain_size = sizeof(General_Header) - prev_recv;

	ssize_t recv_bytes = recv(sockfd, cur_header, remain_size, 0);
	if (recv_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("recv() during header send");
		return -1;
	}

	if (recv_bytes < remain_size){
		recv_req -> recv_header_offset += recv_bytes;
		return 0;
	}

	// otherwise we read the whole header

	char is_fingerprint = recv_req -> header.is_fingerprint;
	uint64_t num_bytes = recv_req -> header.content_size;


	if (TO_LOG_GENERAL_HEADERS){
		INFO(NCCL_NET | NCCL_INIT, "Received General Header:\n\t\t\t\tSockfd: %d\n\t\t\t\tIs fingerprint: %d\n\t\t\t\tContent Size: %lu\n\n", sockfd, (int) is_fingerprint, num_bytes);
	}

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: recv_header()\n");
	}

	return 1;

}

int process_recv_reg_data(Dedup_Recv_Req * recv_req) {

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: recv_reg_data()\n");
	}

	int sockfd = recv_req -> sockfd;

	void * data = recv_req -> app_buffer;
	uint64_t content_size = recv_req -> header.content_size;
	uint64_t offset = recv_req -> app_offset;

	void * cur_data = data + offset;
	uint64_t remain_bytes = content_size - offset;

	if (remain_bytes == 0){
		return 1;
	}

	ssize_t recv_bytes = recv(sockfd, cur_data, remain_bytes, 0);

	if (recv_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("recv() during reg data recv");
		return -1;
	}

	recv_req -> app_offset += recv_bytes;

	// if there are some bytes remaining don't indicate to continue to next stage
	if (recv_req -> app_offset < content_size){
		return 0;
	}

	// otherwise we have finished

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: recv_reg_data()\n");
	}

	return 1;
}


int process_recv_fingerprint_header(Dedup_Recv_Req * recv_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: recv_fingerprint_header()\n");
	}

	int sockfd = recv_req -> sockfd;

	int prev_recv = recv_req -> recv_fingerprint_header_offset;
	void * cur_header = &(recv_req -> fingerprint_header) + prev_recv;
	size_t remain_size = sizeof(Fingerprint_Header) - prev_recv;

	ssize_t recv_bytes = recv(sockfd, cur_header, remain_size, 0);
	if (recv_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("recv() during fingerprint header send");
		return -1;
	}	

	// if we didn't finish sending the header
	if (recv_bytes < remain_size){
		recv_req -> recv_fingerprint_header_offset += recv_bytes;
		return 0;
	}

	// otherwise we recvd the entire header

	// we need to allocate temporary structures to make life easier
	uint64_t num_fingerprints = recv_req -> fingerprint_header.num_fingerprints;
	recv_req -> recv_fingerprint_state.packaged_fingerprints_size_bytes = num_fingerprints * sizeof(Fingerprint);

	recv_req -> recv_fingerprint_state.packaged_fingerprints = malloc(recv_req -> recv_fingerprint_state.packaged_fingerprints_size_bytes);
	if (!recv_req -> recv_fingerprint_state.packaged_fingerprints){
		perror("malloc() for recving packaged fingerprints");
		return -1;
	}

	// assume we will be missing all fingerprints...
	recv_req -> recv_fingerprint_state.missing_fingerprint_inds = malloc(num_fingerprints * sizeof(uint64_t));
	if (!recv_req -> recv_fingerprint_state.missing_fingerprint_inds){
		perror("malloc() for missing fingerprint inds");
		return -1;
	}

	recv_req -> recv_fingerprint_state.missing_fingerprint_slots = malloc(num_fingerprints * sizeof(void *));
	if (!recv_req -> recv_fingerprint_state.missing_fingerprint_slots){
		perror("malloc() for missing fingerprint slots");
		return -1;
	}


	(recv_req -> recv_fingerprint_state).recv_fingerprint_offset = 0;
	(recv_req -> recv_fingerprint_state).missing_fingerprint_header_offset = 0;
	(recv_req -> recv_fingerprint_state).send_missing_fingerprint_inds_offset = 0;
	(recv_req -> recv_fingerprint_state).cur_recv_content_ind = 0;
	(recv_req -> recv_fingerprint_state).cur_recv_content_offset = 0;

	if (TO_LOG_FINGERPRINT_HEADERS){
		INFO(NCCL_NET | NCCL_INIT, "Received Fingerprint Header:\n\t\t\t\tSockfd: %d\n\t\t\t\tNum fingerprints: %lu\n\n", sockfd, num_fingerprints);
	}


	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: recv_fingerprint_header()\n");
	}

	return 1;
}

int process_recv_packaged_fingerprints(Dedup_Recv_Req * recv_req){


	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: recv_packaged_fingerprints()\n");
	}

	int sockfd = recv_req -> sockfd;

	int prev_recv = recv_req -> recv_fingerprint_state.recv_fingerprint_offset;

	void * cur_packaged_fingerprints = ((void *) recv_req -> recv_fingerprint_state.packaged_fingerprints) + prev_recv;

	uint64_t packaged_fingerprints_size_bytes = recv_req -> recv_fingerprint_state.packaged_fingerprints_size_bytes;

	size_t remain_size = packaged_fingerprints_size_bytes - prev_recv;

	ssize_t recv_bytes = recv(sockfd, cur_packaged_fingerprints, remain_size, 0);
	if (recv_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("recv() during packaged fingerprints");
		return -1;
	}

	if (recv_bytes < remain_size){
		recv_req -> recv_fingerprint_state.recv_fingerprint_offset += recv_bytes;
		return 0;
	}

	// otherwise we sent the whole thing so we can continue

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: recv_packaged_fingerprints()\n");
	}

	return 1;
}


int process_populate_from_net_cache(Dedup_Recv_Req * recv_req) {

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: populate_from_net_cache()\n");
	}

	int ret;

	uint64_t num_fingerprints = recv_req -> fingerprint_header.num_fingerprints;
	Fingerprint * packaged_fingerprints = recv_req -> recv_fingerprint_state.packaged_fingerprints;
	uint64_t * missing_fingerprint_inds = recv_req -> recv_fingerprint_state.missing_fingerprint_inds;
	void ** missing_fingerprint_slots = recv_req -> recv_fingerprint_state.missing_fingerprint_slots;


	void * cur_app_buffer = recv_req -> app_buffer;
	uint64_t total_missing_bytes = 0;
	uint64_t total_bytes = 0;
	uint64_t num_missing_fingerprints = 0;

	Fingerprint_Entry entry;
	for (uint64_t i = 0; i < num_fingerprints; i++){

		if (TO_PRINT_FINGERPRINT_INFO){
			printf("Fingerprint #%lu:Content Size: %lu\n\tHex: ", i, packaged_fingerprints[i].content_size);
			print_sha256(packaged_fingerprints[i].fingerprint);
			printf("\n");
		}
		// Blocking call to the Fingerprint hash table part of 
		//	system-wide shared memory global_fingerprint_cache (which == /dev/shm/libnetdedup)
		total_bytes += packaged_fingerprints[i].content_size;
		ret = lookup_fingerprint(global_fingerprint_cache, packaged_fingerprints[i].fingerprint, &entry);
		if (!ret){
			missing_fingerprint_inds[num_missing_fingerprints] = i;
			missing_fingerprint_slots[num_missing_fingerprints] = (void *) cur_app_buffer;
			num_missing_fingerprints++;
			total_missing_bytes += packaged_fingerprints[i].content_size;
		}
		else{
			if (TO_PRINT_FINGERPRINT_INFO){
				printf("Found fingerprint ind #%lu!\n", i);
			}
			copy_fingerprint_content((void *) cur_app_buffer, global_fingerprint_cache, &entry);
			recv_req -> app_filled_size += packaged_fingerprints[i].content_size;
		}
		cur_app_buffer += packaged_fingerprints[i].content_size;
	}


	// maintain global statistics
	// ideally we'd want these to be atomic, for now ok...
	global_fingerprint_cache -> stats.total_recv_bytes += total_bytes;
	global_fingerprint_cache -> stats.populated_from_cache_bytes += (total_bytes - total_missing_bytes);
	global_fingerprint_cache -> stats.total_fingerprints += num_fingerprints;
	global_fingerprint_cache -> stats.total_found_fingerprints += (num_fingerprints - num_missing_fingerprints);

	uint64_t redudant_bytes = total_bytes - total_missing_bytes;
	double redudant_ratio = 100 * ((double) redudant_bytes / (double) total_bytes);


	// set the number of missing fingerprints for next stage to send out
	(recv_req -> recv_fingerprint_state).missing_fingerprint_header.num_missing_fingerprints = num_missing_fingerprints;

	if (TO_LOG_CAPTURE_STATS){
		INFO(NCCL_NET | NCCL_INIT, "Capture stats:\n\tTotal Fingerprints: %llu\n\tMissing Fingerprints: %llu\n\nRedundant Ratio: %llu / %llu\n\tRedundant Percentage: %.2f%%\n\n", num_fingerprints, num_missing_fingerprints, redudant_bytes, total_bytes, redudant_ratio);
	}

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: populate_from_net_cache()\n");
	}

	return 1;
}


int process_send_missing_fingerprint_header(Dedup_Recv_Req * recv_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: send_missing_fingerprint_header()\n");
	}
	

	int sockfd = recv_req -> sockfd;

	int prev_sent = recv_req -> recv_fingerprint_state.missing_fingerprint_header_offset;

	void * cur_header = ((void *) &(recv_req -> recv_fingerprint_state.missing_fingerprint_header)) + prev_sent;

	size_t remain_size = sizeof(Missing_Fingerprint_Header) - prev_sent;

	ssize_t sent_bytes = send(sockfd, cur_header, remain_size, 0);

	if (sent_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("send() during send missing fingerprints header");
		return -1;
	}

	if (sent_bytes < remain_size){
		recv_req -> recv_fingerprint_state.missing_fingerprint_header_offset += sent_bytes;
		return 0;
	}

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: send_missing_fingerprint_header()\n");
	}

	return 1;

}


int process_send_missing_fingerprints(Dedup_Recv_Req * recv_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: recv_missing_content()\n");
	}

	int sockfd = recv_req -> sockfd;

	uint64_t num_missing_fingerprints = recv_req -> recv_fingerprint_state.missing_fingerprint_header.num_missing_fingerprints;

	uint64_t total_size = num_missing_fingerprints * sizeof(uint64_t);

	uint64_t prev_sent = recv_req -> recv_fingerprint_state.send_missing_fingerprint_inds_offset;

	uint64_t * missing_fingerprint_inds = recv_req -> recv_fingerprint_state.missing_fingerprint_inds;
	void * cur_missing_fingerprint_inds = ((void *) missing_fingerprint_inds) + prev_sent;

	uint64_t remain_size = total_size - prev_sent;

	ssize_t sent_bytes = send(sockfd, cur_missing_fingerprint_inds, remain_size, 0);
	if (sent_bytes == -1){
		if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
			return 0;
		}
		perror("send() during missing fingerprints");
		return -1;
	}

	if (sent_bytes < remain_size){
		recv_req -> recv_fingerprint_state.send_missing_fingerprint_inds_offset += sent_bytes;
		return 0;
	}

	// otherwise we have finished sending

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: send_missing_fingerprints()\n");
	}

	return 1;
}


int process_recv_missing_content(Dedup_Recv_Req * recv_req){

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: recv_missing_content()\n");
	}

	int sockfd = recv_req -> sockfd;

	Fingerprint * packaged_fingerprints = (recv_req -> recv_fingerprint_state).packaged_fingerprints;
	uint64_t * missing_fingerprint_inds = (recv_req -> recv_fingerprint_state).missing_fingerprint_inds;
	void ** missing_fingerprint_slots = (recv_req -> recv_fingerprint_state).missing_fingerprint_slots;

	uint64_t num_missing_fingerprints = (recv_req -> recv_fingerprint_state).missing_fingerprint_header.num_missing_fingerprints;

	uint64_t cur_recv_content_ind = (recv_req -> recv_fingerprint_state).cur_recv_content_ind;
	uint64_t cur_recv_content_offset = (recv_req -> recv_fingerprint_state).cur_recv_content_offset;

	void * cur_fingerprint_loc;
	uint64_t fingerprint_content_remain_size;
	Fingerprint_Entry new_entry;
	int ret;

	ssize_t recv_bytes;
	for (int i = cur_recv_content_ind; i < num_missing_fingerprints; i++){
			
		// if we already have received some of this fingerprint then need to insert after
		cur_fingerprint_loc = missing_fingerprint_slots[i] + cur_recv_content_offset;

		// account for remaining size
		fingerprint_content_remain_size = packaged_fingerprints[missing_fingerprint_inds[i]].content_size - cur_recv_content_offset;
		

		recv_bytes = recv(sockfd, cur_fingerprint_loc, fingerprint_content_remain_size, 0);
		
		if (recv_bytes == -1){
			if ((errno = EAGAIN) || (errno == EWOULDBLOCK)){
				return 0;
			}
			perror("recv() during content reply");
			return -1;
		}

		// otherwise we received some bytes

		recv_req -> app_filled_size += recv_bytes;

		// if we've only partially recevied this fingerprint content
		if (recv_bytes < fingerprint_content_remain_size){
			recv_req -> recv_fingerprint_state.cur_recv_content_ind = i;
			recv_req -> recv_fingerprint_state.cur_recv_content_offset = cur_recv_content_offset + recv_bytes;
			return 0;
		}


		// otherwise we've received the whole fingerprint

		// assert recv_req -> app_filled_size = packaged_fingerprints[missing_fingerprint_inds[i]].content_size
		recv_req -> recv_fingerprint_state.cur_recv_content_ind = i + 1;
		recv_req -> recv_fingerprint_state.cur_recv_content_offset = 0;

		cur_recv_content_offset = 0;		
	}


	// if we made it through all the fingerprints we can continue to complete this recv
	// we will process the recv which frees extra space and then test will finalize

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: recv_missing_content()\n");
	}

	return 1;
}


int processs_insert_inbound_fingerprints(Dedup_Recv_Req * recv_req){

	if (to_skip_insert_cache){
		return 1;
	}

	if (TO_LOG_PROTOCOL_INTERNAL_ENTRY_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Entered: insert_inbound_fingerprints()\n");
	}

	if (pthread_mutex_trylock(&(global_fingerprint_cache -> cache_lock)) != 0){
		return 0;
	}

	Fingerprint * packaged_fingerprints = (recv_req -> recv_fingerprint_state).packaged_fingerprints;
	uint64_t * missing_fingerprint_inds = (recv_req -> recv_fingerprint_state).missing_fingerprint_inds;
	uint64_t num_missing_fingerprints = (recv_req -> recv_fingerprint_state).missing_fingerprint_header.num_missing_fingerprints;
	void ** missing_fingerprint_slots = (recv_req -> recv_fingerprint_state).missing_fingerprint_slots;


	Fingerprint_Entry new_entry;
	int ret;
	for (uint64_t i = 0; i < num_missing_fingerprints; i++){
		// insert the content into cache
		ret = insert_fingerprint(global_fingerprint_cache, &(packaged_fingerprints[missing_fingerprint_inds[i]]), missing_fingerprint_slots[i], &new_entry);
		if (ret){
			fprintf(stderr, "CACHE IS FULL\n");
			pthread_mutex_unlock(&(global_fingerprint_cache -> cache_lock));
			to_skip_insert_cache = 1;
			if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
				INFO(NCCL_NET | NCCL_INIT, "Completed: insert_inbound_fingerprints()\n");
			}
			return 1;
		}
	}

	pthread_mutex_unlock(&(global_fingerprint_cache -> cache_lock));

	if (TO_LOG_PROTOCOL_INTERNAL_COMPLETE_VERBOSE){
		INFO(NCCL_NET | NCCL_INIT, "Completed: insert_inbound_fingerprints()\n");
	}
	
	return 1;
}


void process_recv_complete(Dedup_Recv_Req * recv_req){
	return;
}


int process_recv(Dedup_Recv_Req * recv_req){

	int to_continue = 1;
	while (to_continue){
		switch (recv_req -> stage){
			case RECV_HEADER:
				to_continue = process_recv_header(recv_req);
				if (to_continue == 1){
					if (recv_req -> header.is_fingerprint){
						recv_req -> stage = RECV_FINGERPRINT_HEADER;
					}
					else{
						recv_req -> stage = RECV_REG_DATA;
					}
				}
				break;
			case RECV_REG_DATA:
				to_continue = process_recv_reg_data(recv_req);
				if (to_continue == 1){
					recv_req -> stage = RECV_COMPLETE;
				}
				break;
			case RECV_FINGERPRINT_HEADER:
				to_continue = process_recv_fingerprint_header(recv_req);
				if (to_continue == 1){
					recv_req -> stage = RECV_PACKAGED_FINGERPRINTS;
				}
				break;

			case RECV_PACKAGED_FINGERPRINTS:
				to_continue = process_recv_packaged_fingerprints(recv_req);
				if (to_continue == 1){
					recv_req -> stage = POPULATE_FROM_NET_CACHE;
				}
				break;
			case POPULATE_FROM_NET_CACHE:
				to_continue = process_populate_from_net_cache(recv_req);
				if (to_continue == 1){
					recv_req -> stage = SEND_MISSING_FINGERPRINT_HEADER;
				}
				break;
			case SEND_MISSING_FINGERPRINT_HEADER:
				to_continue = process_send_missing_fingerprint_header(recv_req);
				if (to_continue == 1){
					if ((recv_req -> recv_fingerprint_state).missing_fingerprint_header.num_missing_fingerprints == 0){
						recv_req -> stage = RECV_COMPLETE;
					}
					else{
						recv_req -> stage = SEND_MISSING_FINGERPRINTS;
					}
				}
				break;
			case SEND_MISSING_FINGERPRINTS:
				to_continue = process_send_missing_fingerprints(recv_req);
				if (to_continue == 1){
					recv_req -> stage = RECV_MISSING_CONTENT;
				}
				break;
			case RECV_MISSING_CONTENT:
				to_continue = process_recv_missing_content(recv_req);
				if (to_continue == 1){
					recv_req -> stage = INSERT_INBOUND_FINGERPRINTS;
				}
				break;
			case INSERT_INBOUND_FINGERPRINTS:
				to_continue = processs_insert_inbound_fingerprints(recv_req);
				if (to_continue == 1){
					recv_req -> stage = RECV_COMPLETE;
				}
				break;
			case RECV_COMPLETE:
				process_recv_complete(recv_req);
				return 1;
			default:
				fprintf(stderr, "Error: unknown recv request stage: %d...\n", recv_req -> stage);
				return -1;
		}
		if (to_continue == -1){
			fprintf(stderr, "Error: unable to process recv during stage: %d\n", recv_req -> stage);
			return -1;
		}
	}
	// if we made it out here then we didn't return from SEND_COMPLETE and also didn't have error, so return 0
	return 0;
}



ncclResult_t netDedup_irecv(void * recvComm, int n, void ** data, int * sizes, int * tags, void ** mhandles, void ** request) {

	int ret;

	Dedup_Recv_Comm * dedup_recv_comm = (Dedup_Recv_Comm *) recvComm;
	int dev_num = dedup_recv_comm -> dev_num;
	int sockfd = dedup_recv_comm -> fd;

	if (TO_LOG_NCCL_API_RECVS){
		INFO(NCCL_NET | NCCL_INIT, "Calling irecv():\n\tDev: %d\n\tFd: %d\n\tSize: %d", dev_num, sockfd, sizes[0]);
	}

	if (active_fds[sockfd]){
		*request = NULL;
		return ncclSuccess;
	}

	

	Dedup_Req * req = malloc(sizeof(Dedup_Req));
	if (!req){
		perror("malloc() for req for send");
		return ncclSystemError;
	}

	req -> type = RECV_REQ;

	Dedup_Recv_Req * recv_req = malloc(sizeof(Dedup_Recv_Req));
	if (!recv_req){
		perror("malloc() for recv_req");
		return ncclSystemError;
	}

	memset(recv_req, 0, sizeof(Dedup_Recv_Req));

	recv_req -> sockfd = dedup_recv_comm -> fd;
	recv_req -> size = sizes[0];
	recv_req -> app_buffer = data[0];
	recv_req -> app_offset = 0;
	recv_req -> app_filled_size = 0;
	recv_req -> recv_header_offset = 0;
	recv_req -> recv_fingerprint_header_offset = 0;

	recv_req -> stage = RECV_HEADER;

	// process as much as we can
	// recv_req state will be updated for as far as we get
	// it will continue to progress every time "test" is called
	// (could have seperate threads that process asynchronously)
	ret = process_recv(recv_req);

	// if there was an error we need to report it
	if (ret == -1){
		fprintf(stderr, "Error: had an issue when processing recv\n");
		return ncclSystemError;
	}

	// the test function will check if this has completed

	// ensure to save the request
	req -> req = recv_req;
	*request = req;

	active_fds[sockfd] = 1;

	return ncclSuccess;
}





ncclResult_t netDedup_test(void * request, int * done, int * size) {

	*done = 0;

	if (!request){
		*done = 1;
		if (size){
			*size = 0;
		}
		return ncclSuccess;
	}

	Dedup_Req * req = (Dedup_Req *) request;

	ReqType type = req -> type;

	int is_complete;

	int sockfd;

	Dedup_Send_Req * send_req;
	Dedup_Recv_Req * recv_req;

	if (type == SEND_REQ){

		if (TO_LOG_NCCL_API_ALL_TESTS){
			INFO(NCCL_NET | NCCL_INIT, "Called test() for send() with fd: %d\n", ((Dedup_Send_Req *) (req -> req)) -> sockfd);
		}

		send_req = (Dedup_Send_Req *) (req -> req);

		is_complete = process_send(send_req);
		if (is_complete == -1){
			//return ncclSystemError;
			fprintf(stderr, "EXITING (gracefully after cache full for demo)!\n");
			kill(0, SIGKILL);
		}

		sockfd = send_req -> sockfd;
	}

	if (type == RECV_REQ){

		if (TO_LOG_NCCL_API_ALL_TESTS){
			INFO(NCCL_NET | NCCL_INIT, "Called test() for recv() with fd: %d\n", ((Dedup_Send_Req *) (req -> req)) -> sockfd);
		}

		recv_req = (Dedup_Recv_Req *) (req -> req);

		is_complete = process_recv(recv_req);
		if (is_complete == -1){
			//return ncclSystemError;
			fprintf(stderr, "EXITING (gracefully after cache full for demo)!\n");
			kill(0, SIGKILL);
		}

		sockfd = recv_req -> sockfd;
	}


	if (is_complete == 1){

		*done = 1;

		if (TO_LOG_NCCL_API_TEST_COMPLETED){
			INFO(NCCL_NET | NCCL_INIT, "Test() completed on sockfd #%d (type %d)!\n", sockfd, type);
		}

		if (size != NULL){
			// the recv will get freed during from irecvConsumed()
			if (type == SEND_REQ){
				*size = (int) (send_req -> header).content_size;
			}
			else{
				*size = (int) (recv_req -> header).content_size;
			}
		}

		// already freed the inner allocations (for fingerprint sends), but still need to free the container

		if (type == SEND_REQ){


			if (send_req -> header.is_fingerprint){
				free(send_req -> send_fingerprint_state.packaged_fingerprints);
				free(send_req -> send_fingerprint_state.fingerprint_offsets);
				free(send_req -> send_fingerprint_state.missing_fingerprint_inds);
			}
		}
		else{
			if (recv_req -> header.is_fingerprint){
				free(recv_req -> recv_fingerprint_state.packaged_fingerprints);
				free(recv_req -> recv_fingerprint_state.missing_fingerprint_slots);
				free(recv_req -> recv_fingerprint_state.missing_fingerprint_inds);
			}
		}

		free(req -> req);
		free(req);

		active_fds[sockfd] = 0;
	}

	// otherwise is_complete should be zero
	return ncclSuccess;
}


// THIS SHOULD BE INTERNAL ERROR FOR SOCKETS
ncclResult_t netDedup_irecvConsumed(void * recvComm, int n, void * request) {

	if (!recvComm || !request){
		return ncclInternalError;
	}


	Dedup_Recv_Comm * dedup_recv_comm = (Dedup_Recv_Comm *) recvComm;

	if (TO_LOG_NCCL_API_RECV_CONSUMED){
		INFO(NCCL_NET | NCCL_INIT, "Called irecvConsumed() for fd: %d\n", dedup_recv_comm -> fd);
	}

	return ncclInternalError;
}


ncclResult_t netDedup_closeSend(void * sendComm) {

	if (!sendComm){
		return ncclSuccess;
	}

	Dedup_Send_Comm * dedup_send_comm = (Dedup_Send_Comm *) sendComm;

	if (TO_LOG_NCCL_API_CLOSE_CONN){
		INFO(NCCL_NET | NCCL_INIT, "Called closeSend() for fd: %d\n", dedup_send_comm -> fd);
	}

	close(dedup_send_comm -> fd);
	free(dedup_send_comm);

	return ncclSuccess;
}

ncclResult_t netDedup_closeRecv(void * recvComm) {

	if (!recvComm){
		return ncclSuccess;
	}

	Dedup_Recv_Comm * dedup_recv_comm = (Dedup_Recv_Comm *) recvComm;

	if (TO_LOG_NCCL_API_CLOSE_CONN){
		INFO(NCCL_NET | NCCL_INIT, "Called closeRecv() for fd: %d\n", dedup_recv_comm -> fd);
	}

	close(dedup_recv_comm -> fd);
	free(dedup_recv_comm);

	return ncclSuccess;
}







