#ifndef NET_DEVICE_H
#define NET_DEVICE_H

#include "common.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <fcntl.h>
#include <ifaddrs.h>


typedef enum net_device_type {
	SOCKET,
	IB
} NetDeviceType;


typedef struct netdev_interface {
    int     index;
    int     flags;      /* IFF_UP etc. */
    long    speed;      /* Mbps; -1 is unknown */
    int     duplex;     /* DUPLEX_FULL, DUPLEX_HALF, or unknown */
    char    name[IF_NAMESIZE + 1];
} NetDev_Interface;


typedef enum socket_state {
	SOCKET_STATE_NONE,
	SOCKET_STATE_INTIALIZED,
	SOCKET_STATE_ACCEPTING,
	SOCKET_STATE_ACCEPTED,
	SOCKET_STATE_CONNECTING,
	SOCKET_STATE_CONNECTED,
	SOCKET_STATE_READY,
	SOCKET_STATE_CLOSED,
	SOCKET_STATE_ERROR
} SocketState;

typedef struct net_socket_dev {
	struct sockaddr_in sa;
	char if_name[IF_NAMESIZE + 1];
	int if_index;
	int if_flags;
	long if_speed;
	int plugin_dev_num;
	int fd;
	int acceptFd;
	SocketState socket_state;
} Net_Socket_Dev;


typedef struct net_ib_dev {

} Net_Ib_Dev;


typedef struct net_device {
	NetDeviceType device_type;
	void * device;
} Net_Device;



// returns number of devices which are "UP"
int init_net_devices(Net_Socket_Dev * net_devices);



#endif