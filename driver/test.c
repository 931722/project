#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#define IOCTL_RUN_NTT _IO('q', 1)
#define NTT_SIZE 1024

int main(void)
{
    int fd;
    unsigned char buf[NTT_SIZE];
    int ret, i;

    for (i = 0; i < NTT_SIZE; i++)
        buf[i] = i & 0xff;

    fd = open("/dev/ntt_hw", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    ret = ioctl(fd, IOCTL_RUN_NTT, buf);
    if (ret < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    printf("ioctl success\n");

    close(fd);
    return 0;
}