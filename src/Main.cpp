

#include <sys/types.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <stdio.h>
#include <stdarg.h>

#include <iostream>

const char *cloneTun = "/dev/net/tun";
const char *remoteIp = "192.168.76.17";
const short remotePort = 5656;

void Debug(const char *msg, ...)
{
    va_list argp;

    va_start(argp, msg);
    vfprintf(stdout, msg ,argp);
    va_end(argp);
}

int AllocTun(char *tunName, int flags, int &fd)
{
    int err = 0;
    struct ifreq ifr;

    do {
        fd = open(cloneTun, O_RDWR);
        if (fd < 0) {
            err = errno;
            perror("open clone dev failed");
            break;
        }

        memset(&ifr, 0 ,sizeof(ifr));
        ifr.ifr_flags = flags;
        if (*tunName != '\0') {
            strncpy(ifr.ifr_name, tunName, IFNAMSIZ);
        }

        err = ioctl(fd, TUNSETIFF, (void*)&ifr);
        if (err != 0) {
            perror("create tun device failed!");
            break;
        }

        /*
        err = ioctl(fd, TUNSETPERSIST, 1);
        if (err != 0) {
            perror("set tun device persist failed!");
            break;
        }
        */

        strcpy(tunName, ifr.ifr_name);
    } while(0);

    
    Debug("AllocTun, error: %d\n", err);
    return err;
}

int ConnectRemote(const char *serverIP, const short serverPort, int &netFD)
{
    int err = 0;

    struct sockaddr_in remoteAddr;
    memset(&remoteAddr, 0, sizeof(sockaddr_in));
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(serverPort);
    remoteAddr.sin_addr.s_addr = inet_addr(serverIP);

    err = connect(netFD, (struct sockaddr*)&remoteAddr, sizeof(remoteAddr));
    if (err != 0) {
        err = errno;
        perror("connect remote vpn server failed");
    } else {
        Debug("connect to remote vpn success!");
    }

    return err;
}

int ServerPrepare(const char *serverIP, const short serverPort, int &netFD)
{
    int err = 0;

    struct sockaddr_in remoteAddr;
    memset(&remoteAddr, 0, sizeof(sockaddr_in));
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(serverPort);
    remoteAddr.sin_addr.s_addr = inet_addr(serverIP);
    int optval = 1;

    do {
        err = setsockopt(netFD, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval));
        if (err != 0) {
            err = errno;
            Debug("reuse addr failed, err:%d, errstr:%s\n", err, strerror(err));
            break;
        }

        err = bind(netFD, (struct sockaddr*)&remoteAddr, sizeof(sockaddr));
        if (err != 0) {
            break;
        }

        err = listen(netFD, 10);
        if (err != 0) {
            err = errno;
            Debug("listen failed, err:%d, errstr:%s\n", err, strerror(err));
            break;
        }

        Debug("vpn server listen succeed!");
    } while(0);

    return err;
}

int SetNonblocking(int fd)
{
    int err = 0;

    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK) < 0) {
        err = errno;
        Debug("fcntl set nonblock failed, error:%s\n", strerror(err));
    }

    return err;
}

int VPNLoop(int tunFD, int netFD, bool client)
{
    int epollFD = -1;
    int err = -1;
    int MAX_EVENTS = 100;
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];
    int nfds = -1;
    int iter = 0;
    int newSock = -1;
    struct sockaddr_in clientAddr;
    socklen_t addrlen = 0;
    int workFD = -1;

    do {
        epollFD = epoll_create(10);
        if (epollFD < 0) {
            err = errno;
            Debug("epool_create failed, err: %d, errstr: %s\n", err, strerror(err));
            break;
        }

        err = SetNonblocking(netFD);
        if (err != 0) {
            break;
        }

        ev.events = EPOLLIN;
        ev.data.fd = netFD;
        err = epoll_ctl(epollFD, EPOLL_CTL_ADD, netFD, &ev);
        if (err != 0) {
            err = errno;
            Debug("epool_tl failed, err: %d, errstr: %s\n", err, strerror(err));
            break;
        }

        while(1) {
            nfds = epoll_wait(epollFD, events, MAX_EVENTS, -1);
            if (nfds < 0) {
                err = errno;
                Debug("epoll_wait failed, err: %d, errstr: %s\n", err, strerror(err));
                break;
            }

            for (iter = 0; iter < nfds; ++iter) {
                workFD = events[iter].data.fd;
                if (!client) {
                    if (workFD == netFD) {
                        newSock = accept(netFD, (struct sockaddr*)&clientAddr, &addrlen);
                        if (newSock < 0) {
                            err = errno;
                            Debug("accept new socket client failed!");
                            break;
                        }

                        err = SetNonblocking(newSock);
                        if (err < 0) {
                            err = errno;
                            Debug("set nonblocking new socket client failed!");
                            break;
                        }

                        ev.events = EPOLLIN | EPOLLET;
                        ev.data.fd = newSock;
                        err = epoll_ctl(epollFD, EPOLL_CTL_ADD, newSock, &ev);
                        if (err < 0) {
                            err = errno;
                            Debug("epoll_ctl failed,err: %d, errstr: %s\n", err, strerror(err));
                            break;
                        }
                    }
                }
            }
        }

    } while(0);

    return err;
}

int ParseArgs(int argc, char * argv[])
{

}

int AllocSocket(int &sf)
{
    int err = 0;
    sf = socket(AF_INET, SOCK_STREAM, 0);
    if (sf < 0) {
        err = errno;
        perror("create socket failed!");
    }

    return err;
}

int main(int argc, char * argv[])
{
    int err = 0;
    char tunName[IFNAMSIZ] = {0};
    int tunFD = -1;
    int netFD = -1;
    bool client = true;

    do {
        if (strcmp(argv[1], "c") == 0) {
            client = true;
        } else if (strcmp(argv[1], "s") == 0) {
            client = false;
        } else {
            fprintf(stderr, "vpn role not specified!");
            err = EINVAL;
            break;
        }

        err = AllocTun(tunName, IFF_TUN, tunFD);
        if (err != 0) {
            break;
        }
        Debug("Tun Name: %s\n", tunName);

        err = AllocSocket(netFD);
        if (err != 0) {
            break;
        }

        if (client) {
            /* this is a vpn client */
            err = ConnectRemote(remoteIp, remotePort, netFD);
            if (err != 0) {
                break;
            }
        } else {
            /* this is a vpn server */
            err = ServerPrepare(remoteIp, remotePort, netFD);
            if (err != 0) {
                break;
            }
        }

        err = VPNLoop(tunFD, netFD, client);

    } while(0);

    return err;
}



