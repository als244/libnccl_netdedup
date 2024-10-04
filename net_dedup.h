#ifndef NET_DEDUP_H
#define NET_DEDUP_H

#include "common.h"
#include "nccl_net_v8.h"
#include "nccl_net_v7.h"

#include "net_device.h"

#include "fingerprint_cache.h"
#include "fingerprint_table.h"
#include "fingerprint.h"

typedef struct net_dedup_state {
	int num_net_devices;
	Net_Socket_Dev net_devices[MAX_NET_DEDUP_DEVS];
	Fingerprint_Cache * global_fingerprint_cache;
	ncclDebugLogger_t logFunction;
} Net_Dedup_State;


typedef struct dedup_listen_comm {
	int dev_num;
	int listenFd;
	int acceptedFd;
	struct sockaddr_in src_addr;
} Dedup_Listen_Comm;

typedef struct dedup_send_comm {
	int dev_num;
	int fd;
	struct sockaddr_in dest_addr;
} Dedup_Send_Comm;

typedef struct dedup_recv_comm {
	int dev_num;
	int fd;
	struct sockaddr_in src_addr;
} Dedup_Recv_Comm;


typedef struct dedup_connect_handle {
	struct sockaddr_in addr;
	int connectingFd;
	int in_progress;
	int is_connected;
} Dedup_Connect_Handle;



typedef enum recv_req_stage {
	RECV_HEADER,
	RECV_REG_DATA,
	RECV_FINGERPRINT_HEADER,
	RECV_PACKAGED_FINGERPRINTS,
	POPULATE_FROM_NET_CACHE,
	SEND_MISSING_FINGERPRINT_HEADER,
	SEND_MISSING_FINGERPRINTS,
	RECV_MISSING_CONTENT,
	RECV_COMPLETE
} RecvReqStage;

typedef enum send_req_stage {
	SEND_HEADER,
	SEND_REG_DATA,
	COMPUTE_FINGERPRINTS,
	SEND_FINGERPRINT_HEADER,
	SEND_PACKAGED_FINGERPRINTS,
	RECV_MISSING_FINGERPRINT_HEADER,
	RECV_MISSING_FINGERPRINTS,
	SEND_MISSING_CONTENT,
	SEND_COMPLETE
} SendReqStage;


typedef struct dedup_header {
	char is_fingerprint;
	uint64_t content_size;
} Dedup_Header;

typedef struct fingerprint_header {
	uint64_t num_fingerprints;
} Fingerprint_Header;

typedef struct missing_fingerprint_header {
	uint64_t num_missing_fingerprints;
} Missing_Fingerprint_Header;

typedef struct fingerprint_recv_state {
	Fingerprint * packaged_fingerprints;
	uint64_t packaged_fingerprints_size_bytes;
	uint64_t recv_fingerprint_offset;
	// tracking which indices within fingerprint sequence were missing
	uint64_t * missing_fingerprint_inds;
	// maintaining where the missing content should go within app buffer
	void ** missing_fingerprint_slots;
	// sending to other side
	Missing_Fingerprint_Header missing_fingerprint_header;
	// in case we could only send partial offset
	int missing_fingerprint_header_offset;
	// if we only paritally sends some of the missing fingerprint inds this iteration
	uint64_t send_missing_fingerprint_inds_offset;
	// when we are acutally receiving reply use this to track when we are 
	// are done (i.e. cur_recv_content_ind == num_missing_fingerprints)
	// (or done after sending num_missing_fingerprints == 0)
	uint64_t cur_recv_content_ind;
	uint64_t cur_recv_content_offset;
} Fingerprint_Recv_State;

typedef struct fingerprint_send_state {
	Fingerprint * packaged_fingerprints;
	Fingerprint_Entry * content_refs;
	uint64_t packaged_fingerprints_size_bytes;
	uint64_t send_fingerprint_offset;
	// receved from other side
	Missing_Fingerprint_Header missing_fingerprint_header;
	// in case we could only recv partial offset
	int missing_fingerprint_header_offset;
	uint64_t * missing_fingerprint_inds;
	// if we only paritally receive some of the missing fingerprint inds this iteration
	uint64_t recv_missing_fingerprint_inds_offset;
	uint64_t cur_reply_content_fingerprint_ind;
	uint64_t cur_reply_content_fingerprint_offset;
} Fingerprint_Send_State;

typedef struct dedup_recv_req {
	int sockfd;
	RecvReqStage stage;
	Dedup_Header header;
	int recv_header_offset;
	Fingerprint_Header fingerprint_header;
	// if we couldn't receive the whole fingerprint header
	int recv_fingerprint_header_offset;
	Fingerprint_Recv_State recv_fingerprint_state;
	uint64_t size;
	void * app_buffer;
	uint64_t app_offset;
	// for debugging purposes dealing with partially filling content before reply
	uint64_t app_filled_size;
} Dedup_Recv_Req;


typedef struct dedup_send_req {
	int sockfd;
	SendReqStage stage;
	Dedup_Header header;
	int send_header_offset;
	Fingerprint_Header fingerprint_header;
	// if we couldn't send the whole fingerprint header
	int send_fingerprint_header_offset;
	Fingerprint_Send_State send_fingerprint_state;
	uint64_t size;
	void * data;
	uint64_t offset;
} Dedup_Send_Req;

typedef enum req_type {
	SEND_REQ,
	RECV_REQ
} ReqType;

typedef struct dedup_req {
	ReqType type;
	void * req;
} Dedup_Req;


extern Net_Dedup_State net_dedup_state;
extern ncclDebugLogger_t nccl_log_func;


// EXPORTED NCCL FUNCTIONS USED AS PLUGIN!


ncclResult_t netDedup_init(ncclDebugLogger_t logFunction);
ncclResult_t netDedup_devices(int * ndev);
ncclResult_t netDedup_getProperties_v8(int dev, ncclNetProperties_v8_t * props);
ncclResult_t netDedup_listen(int dev, void * handle, void ** listenComm);
ncclResult_t netDedup_connect_v8(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v8_t** sendDevComm);
ncclResult_t netDedup_accept_v8(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v8_t ** recvDevComm);
ncclResult_t netDedup_regMr_v8(void * comm, void * data, size_t size, int type, void ** mhandle);
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

ncclResult_t netDedup_getProperties_v7(int dev, ncclNetProperties_v7_t * props);
ncclResult_t netDedup_connect_v7(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v7_t** sendDevComm);
ncclResult_t netDedup_accept_v7(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v7_t ** recvDevComm);
ncclResult_t netDedup_regMr_v7(void * comm, void * data, int size, int type, void ** mhandle);

#endif