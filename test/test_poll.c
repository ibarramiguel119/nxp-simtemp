#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>

#define DEVICE "/dev/simtemp"

int main(void) {
    int fd = open(DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    int epfd = epoll_create1(0);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLPRI;
    ev.data.fd = fd;

    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

    struct epoll_event events[1];

    while (1) {
        int n = epoll_wait(epfd, events, 1, -1);
        if (n > 0) {
            if (events[0].events & EPOLLIN)
                printf("Nuevo sample disponible\n");
            if (events[0].events & EPOLLPRI)
                printf("Threshold crossed!\n");
        }
    }

    close(fd);
    close(epfd);
    return 0;
}
