

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <iostream>

static short PORT = 5656;
static const char *IP = "127.0.0.1";

void ErrMsg(const char *fmt, ...)
{ 
    va_list ag;
    va_start(ag, fmt);
    vfprintf(stderr, fmt, ag);
    va_end(ag);
}

int StartClient()
{
    int fd = -1;
    int err = 0;
    const char *data = "hello this is udp client!";
    struct sockaddr_in serverAddr;

    do {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            err = errno;
            ErrMsg("create socket failed %s", strerror(err));
            break;
        }

        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(IP);
        serverAddr.sin_port = htons(PORT);

        sendto(fd, data, strlen(data), 0, (struct sockaddr*)&serverAddr, sizeof(sockaddr));

    } while(0);

    if (-1 != fd) {
        close(fd);
        fd = -1;
    }

    return err;
}

int StartServer()
{
    int fd = -1;
    int err = 0;
    char data[100] = {0};
    struct sockaddr_in serverAddr;
    struct sockaddr_in clientAddr;;
    socklen_t sockLen = 0;

    do {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            err = errno;
            ErrMsg("create socket failed %s", strerror(err));
            break;
        }

        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = inet_addr(IP);
        serverAddr.sin_port = htons(PORT);

        err = bind(fd, (struct sockaddr*)&serverAddr, sizeof(sockaddr));
        if (0 != err) {
            err = errno;
            ErrMsg("bind socket failed %s", strerror(err));
            break;
        }

        recvfrom(fd, data, 99, 0, (struct sockaddr*)&clientAddr, &sockLen);

        printf("%s\n", data);

    } while(0);

    if (-1 != fd) {
        close(fd);
        fd = -1;
    }

    return err;
}

int StartUp(bool client)
{
    int err = 0;

    if (client) {
        err = StartClient();
    } else {
        err = StartServer();
    }

    return err;
}

int main(int argc, char *argv[])
{
    int err = 0;
    bool client = false;

    do {
        if (2 != argc) {
            err = EINVAL;
            ErrMsg("%s %s\n", "Invalid argument count", "aa");
            break;
        }

        if (strcmp(argv[1], "c") == 0) {
            client = true;
        } else if (strcmp(argv[1], "s") == 0) {
            client = false;
        } else {
            err = EINVAL;
            ErrMsg("%s\n", "invalid role");
            break;
        }

        err = StartUp(client);

    } while(0);

    return err;
}


