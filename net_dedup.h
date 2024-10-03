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
	int fd;
	int has_confirmed_reg;
	int ctrlFd;
	struct sockaddr_in src_addr;
} Dedup_Listen_Comm;

typedef struct dedup_send_comm {
	int dev_num;
	int fd;
	int ctrlFd;
	struct sockaddr_in dest_addr;
} Dedup_Send_Comm;

typedef struct dedup_recv_comm {
	int dev_num;
	int fd;
	int ctrlFd;
	struct sockaddr_in src_addr;
} Dedup_Recv_Comm;


typedef struct dedup_connect_handle {
	struct sockaddr_in addr;
	int connectingFd;
	int ctrlFd;
	int in_progress;
	int in_progress_ctrl;
	int is_connected;
	int is_sent_connected;
	int is_ctrl_connected;
} Dedup_Connect_Handle;


extern Net_Dedup_State net_dedup_state;
extern ncclDebugLogger_t nccl_log_func;


// EXPORTED NCCL FUNCTIONS USED AS PLUGIN!


ncclResult_t netDedup_init(ncclDebugLogger_t logFunction);
ncclResult_t netDedup_devices(int * ndev);
ncclResult_t netDedup_getProperties_v8(int dev, ncclNetProperties_v8_t * props);
ncclResult_t netDedup_listen(int dev, void * handle, void ** listenComm);
ncclResult_t netDedup_connect_v8(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v8_t** sendDevComm);
ncclResult_t netDedup_accept_v8(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v8_t ** recvDevComm);
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

ncclResult_t netDedup_getProperties_v7(int dev, ncclNetProperties_v7_t * props);
ncclResult_t netDedup_connect_v7(int dev, void * handle, void ** sendComm, ncclNetDeviceHandle_v7_t** sendDevComm);
ncclResult_t netDedup_accept_v7(void * listenComm, void ** recvComm, ncclNetDeviceHandle_v7_t ** recvDevComm);
ncclResult_t netDedup_regMr_v7(void * comm, void * data, int size, int type, void ** mhandle);

#endif