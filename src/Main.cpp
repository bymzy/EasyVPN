

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
#include <assert.h>
#include <getopt.h>

#include <iostream>

const int MAX_BUF_SIZE = 2000;
const char *cloneTun = "/dev/net/tun";


void Error(const char *fmt, ...)
{ 
    va_list ag;
    va_start(ag, fmt);
    vfprintf(stderr, fmt, ag);
    va_end(ag);
}

void Debug(const char *fmt, ...)
{
    va_list argp;

    va_start(argp, fmt);
    vfprintf(stdout, fmt,argp);
    va_end(argp);
}

void Usage()
{
    Error("Usage: UDPTun -i <remoteIP> -p <remotePort> -t <localPort> [-h]\n");
    Error("        -i remote udp ip\n");
    Error("        -p remote udp port\n");
    Error("        -t local udp port\n");
}

int AllocTun(char *tunName, int flags, int &fd)
{
    int err = 0;
    struct ifreq ifr;

    do {
        fd = open(cloneTun, O_RDWR);
        if (fd <= 0) {
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

        
        err = ioctl(fd, TUNSETPERSIST, 1);
        if (err != 0) {
            perror("set tun device persist failed!");
            break;
        }
        

        strcpy(tunName, ifr.ifr_name);
    } while(0);

    
    //Debug("AllocTun, error: %d, tunfd: %d \n", err, fd);
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

int RecvNBytes(int fd, char *buf, uint32_t toRecv)
{
    Debug("RecvNBytes going to recv %d bytes\n", toRecv);
    int err = 0;
    uint32_t totalRecved = 0;
    ssize_t recved = 0;
    struct sockaddr_in remoteAddr;
    socklen_t sockLen = 0;

    while (toRecv > 0) {

        do {
            recved = recvfrom(fd, buf + totalRecved, toRecv, 0,
                    (struct sockaddr*)&remoteAddr, &sockLen);
        } while(recved < 0 && ((errno == EAGAIN) || (errno == EWOULDBLOCK)));

        if (recved < 0) {
            err = errno;
            break;
        } else if (recved == 0) {
            err = EINVAL;
            break;
        }

        toRecv -= recved;
        totalRecved += recved;
    }

    return err;
}

int SendNBytes(int fd, char *buf, uint32_t toSend, struct sockaddr_in *remoteAddr)
{
    Debug("SendNBytes going to send \%d bytes\n", toSend);
    int err = 0;
    uint32_t totalSent = 0;
    ssize_t sent = 0;

    while (toSend > 0) {
        do {
            sent = sendto(fd, buf + totalSent, toSend, 0,
                    (struct sockaddr*)remoteAddr, sizeof(sockaddr));
        } while(sent < 0 && ((errno == EAGAIN) || (errno == EWOULDBLOCK)));

        if (sent < 0) {
            err = errno;
            break;
        } else if (0 == sent) {
            err = EINVAL;
            break;
        }

        toSend -= sent;
        totalSent += sent;
    }

    return err;
}

int ServerPrepare(const char *serverIP, const short serverPort, int netFD)
{
    int err = 0;

    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(sockaddr_in));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(serverPort);
    localAddr.sin_addr.s_addr = inet_addr(serverIP);
    int optval = 1;
    socklen_t clientSockAddrLen;
    struct sockaddr_in clientSockAddr;

    do {
        err = setsockopt(netFD, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval));
        if (err != 0) {
            err = errno;
            Debug("reuse addr failed, err:%d, errstr:%s\n", err, strerror(err));
            break;
        }

        err = bind(netFD, (struct sockaddr*)&localAddr, sizeof(sockaddr));
        if (err != 0) {
            break;
        }

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

int VPNLoop(int tunFD, int netFD, struct sockaddr_in *remoteAddr)
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
    char buf[MAX_BUF_SIZE];
    ssize_t readed = 0;
    ssize_t sent = 0;
    uint32_t packetLength = 0;

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

        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = netFD;
        err = epoll_ctl(epollFD, EPOLL_CTL_ADD, netFD, &ev);
        if (err != 0) {
            err = errno;
            Debug("epool_tl failed, err: %d, errstr: %s\n", err, strerror(err));
            break;
        }

        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = tunFD;
        err = epoll_ctl(epollFD, EPOLL_CTL_ADD, tunFD, &ev);
        if (err != 0) {
            err = errno;
            Debug("epool_tl failed, err: %d, errstr: %s\n", err, strerror(err));
            break;
        }

        while(1) {
            nfds = epoll_wait(epollFD, events, MAX_EVENTS, -1);
            if (nfds < 0) {
                err = errno;
                if (EINTR == err) {
                    err = 0;
                    Debug("interupt!!!");
                    continue;
                }
                Debug("epoll_wait failed, err: %d, errstr: %s\n", err, strerror(err));
                break;
            }

            for (iter = 0; iter < nfds; ++iter) {
                workFD = events[iter].data.fd;
                if (workFD == netFD) {
                    uint32_t temp;
                    packetLength = 0;
                    err = RecvNBytes(netFD, (char *)&packetLength, sizeof(packetLength));
                    if (err != 0) {
                        Debug("recv from netfd failed!, err: %d, errstr: %s\n",
                                err, strerror(err));
                        break;
                    }
                    temp = ntohl(packetLength);
                    Debug("going to recv %d bytes from netfd\n", temp);

                    err = RecvNBytes(netFD, buf, temp);
                    if (err != 0) {
                        Debug("recv packet failed, err: %d, errstr: %s\n",
                                err, strerror(err));
                        break;
                    }

                    write(tunFD, buf, temp);
                    Debug("Write %d bytes to tun \n", temp);
                } else if (workFD == tunFD) {

                    packetLength = 0;
                    readed = read(tunFD, buf, MAX_BUF_SIZE); 
                    Debug("read %d bytes from tun \n", readed);
                    packetLength = htonl(readed);

                    err = SendNBytes(netFD, (char *)&packetLength, sizeof(packetLength), remoteAddr);
                    if (0 != err) {
                        Debug("send to network failed, error: %d, errstr: %s\n", err, strerror(err));
                        break;
                    }
                    Debug("send packetLength %d \n", packetLength);

                    err = SendNBytes(netFD, buf, readed, remoteAddr);
                    if (0 != err) {
                        Debug("send to network failed, error: %d, errstr: %s\n", err, strerror(err));
                        break;
                    }
                }
            }

            if (err != 0) {
                break;
            }
        }

    } while(0);

    return err;
}

int ParseArgs(int argc, char * argv[], bool& client, short& localPort,
        char *remoteIP, short& remotePort, char *tunName)
{
    int err = 0;
    int opt;
    int tempPort = 0;
    bool roleSet = false;
    bool remoteIPSet = false;
    bool remotePortSet = false;
    bool localPortSet = false;
    bool tunNameSet = false;

    while ((opt = getopt(argc, argv, "r:i:p:t:d:h")) != -1) {
        switch (opt) {
            case 'r':
                if (strcmp("s", (char*)optarg) == 0) {
                    client = false;
                } else if ("c", (char*)optarg) {
                    client = true;
                } else {
                    err = EINVAL;
                    Error("invalid role arg %s \n", (char*)optarg);
                }
                roleSet = true;
                break;
            case 'i':
                strcpy(remoteIP, (char*)optarg);
                struct in_addr addr;
                if (inet_aton(remoteIP, &addr) == 0) {
                    err = EINVAL;
                    Error("invalid remote ip %s \n", (char*)optarg);
                }
                remoteIPSet = true;
                break;
            case 't':
                tempPort = atoi((char*)optarg);
                if (tempPort <= 0 && tempPort >= 65536) {
                    err = EINVAL;
                }
                localPort = (short)tempPort;
                localPortSet = true;
                break;
            case 'p':
                tempPort = atoi((char*)optarg);
                if (tempPort <= 0 && tempPort >= 65536) {
                    err = EINVAL;
                }
                remotePort = (short)tempPort;
                remotePortSet = true;
                break;
            case 'd':
                strcpy(tunName, (char*)optarg);
                tunNameSet = true;
                break;
            case 'h':
                Usage();
                break;
        }

        if (0 != err) {
            break;
        }
    }

    /* TODO check every args used */

    return err;
}

int AllocSocket(int &sf)
{
    int err = 0;
    sf = socket(AF_INET, SOCK_DGRAM, 0);
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
    //sprintf(tunName, "tun0", 4);

    int tunFD = -1;
    int netFD = -1;
    int clientSock = -1;
    bool client = true;

    short localPort = 0;
    char remoteIP[16];
    short remotePort = 0;
    struct sockaddr_in remoteAddr;

    do {
        err = ParseArgs(argc, argv, client, localPort, remoteIP, remotePort, tunName);
        if (err != 0) {
            break;
        }

        Debug("args: ip: %s, local port: %d, remote port: %d\n", remoteIP, localPort, remotePort);

        err = AllocTun(tunName, IFF_TUN | IFF_NO_PI, tunFD);
        if (err != 0) {
            break;
        }
        Debug("Tun Name: %s, tunFD: %d \n", tunName, tunFD);

        err = AllocSocket(netFD);
        if (err != 0) {
            break;
        }

        /* this is a vpn server */
        err = ServerPrepare("0.0.0.0", localPort, netFD);
        if (err != 0) {
            break;
        }

        Debug("VPNLoop tunFD: %d, netFD: %d \n", tunFD, netFD);

        remoteAddr.sin_family = AF_INET;
        remoteAddr.sin_addr.s_addr = inet_addr(remoteIP);
        remoteAddr.sin_port = htons(remotePort);

        err = VPNLoop(tunFD, netFD, &remoteAddr);

    } while(0);

    if (netFD > 0) {
        close(netFD);
    }

    if (tunFD > 0) {
        close(tunFD);
    }

    return err;
}


