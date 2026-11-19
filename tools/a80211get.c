// Raw Apple80211 get: a80211get <ifname> <type> <len> [repeat seconds]
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
struct a80211req { char ifname[16]; int type; int val; unsigned len; unsigned pad; void *data; };
int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    int s = socket(AF_INET, SOCK_DGRAM, 0), n = argc > 4 ? atoi(argv[4]) : 1; unsigned len = strtoul(argv[3], 0, 0);
    unsigned char *b = calloc(1, len + 16), last[4096] = {0}; int first = 1;
    for (int i = 0; i < n * 4; i++) {
        struct a80211req r = {0}; strlcpy(r.ifname, argv[1], 16); r.type = atoi(argv[2]); r.len = len; r.data = b; memset(b, 0, len);
        int e = ioctl(s, 0xc02869c9UL, &r);
        if (first || memcmp(b, last, len < 4096 ? len : 4096)) {
            printf("t+%.2fs type %d -> %d errno=%d val=%d len=%u:", i / 4.0, r.type, e, e ? errno : 0, r.val, r.len);
            for (unsigned j = 0; j < len && j < 96; j++) printf("%s%02x", j % 4 ? "" : " ", b[j]); printf("\n");
            memcpy(last, b, len < 4096 ? len : 4096); first = 0;
        }
        if (n > 1) usleep(250000);
    }
}
