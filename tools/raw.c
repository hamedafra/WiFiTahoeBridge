// Raw Apple80211 scan test: raw <req_len> <uuid 0|1> <result_len>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
struct a80211req { char ifname[16]; int type; int val; unsigned len; void *data; };
#define SIOCSA80211 _IOW(0x69, 200, struct a80211req)
#define SIOCGA80211 _IOWR(0x69, 201, struct a80211req)
int main(int argc, char **argv) {
    unsigned len = strtoul(argv[1], 0, 0), uuid = atoi(argv[2]), rlen = strtoul(argv[3], 0, 0);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    unsigned char *b = calloc(1, 0x4000);
    b[0] = 1; b[4] = 3; b[0x34] = 1; b[0x38] = 1;
    if (uuid) memcpy(b + 0x154c, "8EDF69AB-78C2-4DA1-81E4-7B86382BD6D6", 36);
    struct a80211req r = {0}; strcpy(r.ifname, "en1"); r.type = 10; r.len = len; r.data = b;
    int e = ioctl(s, SIOCSA80211, &r);
    printf("SCAN_REQ len=0x%x uuid=%u -> %d errno=%d\n", len, uuid, e, e ? errno : 0);
    if (e) return 1;
    int n = 0, tries = 0;
    unsigned char *res = calloc(1, 0x4000);
    while (tries < 40) {
        struct a80211req g = {0}; strcpy(g.ifname, "en1"); g.type = 11; g.len = rlen; g.data = res;
        e = ioctl(s, SIOCGA80211, &g);
        if (e == 0) { n++; continue; }
        if (n) break;
        tries++; usleep(250000);
    }
    printf("SCAN_RESULT len=0x%x -> %d results (last errno=%d)\n", rlen, n, errno);
    return 0;
}
