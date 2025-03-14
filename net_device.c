#include "net_device.h"


static int get_interface_common(const int fd, struct ifreq *const ifr, NetDev_Interface *const info)
{
    struct ethtool_cmd  cmd;
    int                 result;

    /* Interface flags. */
    if (ioctl(fd, SIOCGIFFLAGS, ifr) == -1)
        info->flags = 0;
    else
        info->flags = ifr->ifr_flags;

    ifr->ifr_data = (void *)&cmd;
    cmd.cmd = ETHTOOL_GSET; /* "Get settings" */
    if (ioctl(fd, SIOCETHTOOL, ifr) == -1) {
        /* Unknown */
        info->speed = -1L;
        info->duplex = DUPLEX_UNKNOWN;
    } else {
        info->speed = ethtool_cmd_speed(&cmd);
        info->duplex = cmd.duplex;
    }

    do {
        result = close(fd);
    } while (result == -1 && errno == EINTR);
    if (result == -1)
        return errno;

    return 0;
}

int get_interface_by_index(const int index, NetDev_Interface *const info)
{
    int             socketfd, result;
    struct ifreq    ifr;

    if (index < 1 || !info)
        return errno = EINVAL;

    socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socketfd == -1)
        return errno;

    ifr.ifr_ifindex = index;
    if (ioctl(socketfd, SIOCGIFNAME, &ifr) == -1) {
        do {
            result = close(socketfd);
        } while (result == -1 && errno == EINTR);
        return errno = ENOENT;
    }

    info->index = index;
    strncpy(info->name, ifr.ifr_name, IF_NAMESIZE);
    info->name[IF_NAMESIZE] = '\0';

    return get_interface_common(socketfd, &ifr, info);
}

int get_interface_by_name(const char *const name, NetDev_Interface *const info)
{
    int             socketfd, result;
    struct ifreq    ifr;

    if (!name || !*name || !info)
        return errno = EINVAL;

    socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socketfd == -1)
        return errno;

    strncpy(ifr.ifr_name, name, IF_NAMESIZE);
    if (ioctl(socketfd, SIOCGIFINDEX, &ifr) == -1) {
        do {
            result = close(socketfd);
        } while (result == -1 && errno == EINTR);
        return errno = ENOENT;
    }

    info->index = ifr.ifr_ifindex;
    strncpy(info->name, name, IF_NAMESIZE);
    info->name[IF_NAMESIZE] = '\0';

    return get_interface_common(socketfd, &ifr, info);
}



int init_net_socket_devs(Net_Socket_Dev * net_devices) {

    int ret;

    struct ifaddrs * ifap;
    ret = getifaddrs(&ifap);
    if (ret){
        perror("getifaddrs()");
        return -1;
    }


    struct ifaddrs * cur_addr = ifap;
    
    NetDev_Interface cur_if;

    int num_active_devs = 0;

    int i = 0;
    char tempPath[PATH_MAX];
    while (cur_addr && num_active_devs < MAX_NET_DEDUP_DEVS){

        if ((cur_addr -> ifa_addr -> sa_family != AF_INET) || (strlen(cur_addr -> ifa_name) < 3) || (strncmp(cur_addr -> ifa_name, "eno", 3) != 0)){
            cur_addr = cur_addr -> ifa_next;
            continue;
        }

        ret = get_interface_by_name(cur_addr -> ifa_name, &cur_if);
        if (ret){
            fprintf(stderr, "Error: unable to get info for interface: %s\n", cur_addr -> ifa_name);
            return -1;
        }

        if ((cur_if.flags & IFF_UP) && ((cur_if.flags & IFF_LOOPBACK) == 0)){

            memcpy(&(net_devices[num_active_devs].sa), (struct sockaddr_in *) cur_addr -> ifa_addr, sizeof(struct sockaddr_in));
            strncpy(net_devices[num_active_devs].if_name, cur_addr -> ifa_name, IF_NAMESIZE);

            net_devices[num_active_devs].if_index = cur_if.index;
            net_devices[num_active_devs].if_flags = cur_if.flags;
            net_devices[num_active_devs].if_speed = cur_if.speed;


            net_devices[num_active_devs].plugin_dev_num = num_active_devs;

            net_devices[num_active_devs].socket_state = SOCKET_STATE_NONE;

            // get path to symlink
            snprintf(tempPath, PATH_MAX, "/sys/class/net/%s/device", cur_addr -> ifa_name);

            // get realpath
            char * res = realpath(tempPath, net_devices[num_active_devs].pciPath);
            if (!res){
                fprintf(stderr, "Error: unable to resolve real path...\n");
                return -1;
            }

            num_active_devs++;
        }

        cur_addr = cur_addr -> ifa_next;

        i++;
    }

    freeifaddrs(ifap);

    return num_active_devs;

}



