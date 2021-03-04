#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

char buf[512];

void
cat(int fd)
{
    int n = read(fd, buf, sizeof(buf));
    while (n > 0) {
        if (write(1, buf, n) != n) {
            printf("cat: write error.\n");
            return;
        }
    }
    if (n < 0) {
        printf("cat: read error.\n");
        return;
    }
}

int
main(int argc, char* argv[])
{
    if (argc <= 1) _exit(-1);

    for (int i = 1; i < argc; ++i) {
        int fd = open(argv[i], 0);
        if (fd < 0) {
            printf("cat: cannot open %s.\n", argv[i]);
            _exit(-1);
        }
        cat(fd);
        close(fd);
    }
    _exit(0);
}
