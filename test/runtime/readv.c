#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#define EXPECT_STRN_EQUAL(expected, actual_str, actual_len) \
    if (strncmp(expected, actual_str, actual_len) != 0) { \
        char actual_buf[actual_len + 1]; \
        memcpy(actual_buf, actual_str, actual_len); \
        actual_buf[actual_len] = '\0'; \
        printf("\"%s\" != \"%s\" -- failed at %s:%d\n", expected, actual_buf, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    }

#define EXPECT_LONG_EQUAL(expected, actual) \
    if (expected != actual) { \
        printf("\"%ld\" != \"%ld\" -- failed at %s:%d\n", (long)expected, (long)actual, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    }

int main()
{
    struct iovec iovs[3];
    char onev[4], twov[4], threev[4];
    iovs[0].iov_base = onev;
    iovs[1].iov_base = twov;
    iovs[2].iov_base = threev;
    iovs[0].iov_len = iovs[1].iov_len = iovs[2].iov_len = 4;

    int fd = open("hello", O_RDWR);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    int startpos = 4;
    int rv = lseek(fd, startpos, SEEK_SET);
    if (rv < 0) {
        perror("lseek");
        exit(EXIT_FAILURE);
    }

    rv = readv(fd, iovs, 3);
    if (rv < 0) {
        perror("readv\n");
        exit(EXIT_FAILURE);
    }
    int bytes_read = rv;

    EXPECT_STRN_EQUAL("one ", iovs[0].iov_base, 4);
    EXPECT_STRN_EQUAL("six ", iovs[1].iov_base, 4);
    EXPECT_STRN_EQUAL("four", iovs[2].iov_base, 4);

    rv = lseek(fd, 0, SEEK_CUR);
    if (rv < 0) {
        perror("lseek");
        exit(EXIT_FAILURE);
    }
    int curpos = rv;
    EXPECT_LONG_EQUAL(startpos + bytes_read, curpos);

    if (lseek(fd, 0, SEEK_END) < 0) {
        perror("lseek(end)");
        exit(EXIT_FAILURE);
    }
    rv = readv(fd, iovs, 3);
    if (rv != 0) {
        printf("readv at end of file returned %d\n", rv);
        exit(EXIT_FAILURE);
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN | POLLOUT;
    rv = poll(&pfd, 1, 0);
    if (rv != 1) {
        printf("unexpected poll return value %d\n", rv);
        exit(EXIT_FAILURE);
    }
    if (pfd.revents != (POLLIN | POLLOUT)) {
        printf("unexpected poll events 0x%x\n", pfd.revents);
        exit(EXIT_FAILURE);
    }

    if (close(fd) < 0) {
        perror("close");
        exit(EXIT_FAILURE);
    }

    fd = open("hello", O_WRONLY);
    if (fd < 0) {
        perror("open write-only");
        exit(EXIT_FAILURE);
    }
    if (readv(fd, iovs, 3) != -1) {
        printf("Could readv from write-only file\n");
        exit(EXIT_FAILURE);
    } else if (errno != EBADF) {
        perror("readv from write-only file: unexpected error");
        exit(EXIT_FAILURE);
    }
    if (close(fd) < 0) {
        perror("close write-only");
        exit(EXIT_FAILURE);
    }

    void *fault_addr = (void *)0xbadf0000;
    fd = open(fault_addr, O_RDWR);
    if ((fd != -1) || (errno != EFAULT)) {
        printf("open with faulting buffer test failed (%d, %d)\n", fd, errno);
        exit(EXIT_FAILURE);
    }
    fd = open("hello", O_RDWR);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    rv = read(fd, fault_addr, 1);
    if ((rv != -1) || (errno != EFAULT)) {
        printf("read with faulting buffer test failed (%d, %d)\n", rv, errno);
        exit(EXIT_FAILURE);
    }
    rv = readv(fd, fault_addr, 1);
    if ((rv != -1) || (errno != EFAULT)) {
        printf("readv with faulting iov test failed (%d, %d)\n", rv, errno);
        exit(EXIT_FAILURE);
    }
    iovs[0].iov_base = fault_addr;
    rv = readv(fd, iovs, 3);
    if ((rv != -1) || (errno != EFAULT)) {
        printf("readv with faulting iov_base test failed (%d, %d)\n", rv, errno);
        exit(EXIT_FAILURE);
    }
    close(fd);

    printf("readv test PASSED\n");

    return EXIT_SUCCESS;
}
