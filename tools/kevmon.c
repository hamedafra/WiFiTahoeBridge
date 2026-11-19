// Prints every IEEE 802.11 kernel event (vendor Apple, class 6) the Wi-Fi family posts. kevmon [seconds]
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/kern_event.h>
#include <sys/socket.h>
#include <sys/time.h>
int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    int s = socket(PF_SYSTEM, SOCK_RAW, SYSPROTO_EVENT);
    struct kev_request f = { KEV_VENDOR_APPLE, 6, KEV_ANY_SUBCLASS };
    if (s < 0 || ioctl(s, SIOCSKEVFILT, &f)) { perror("kev"); return 1; }
    alarm(argc > 1 ? atoi(argv[1]) : 20);
    static unsigned char b[0x4000];
    for (;;) {
        ssize_t n = recv(s, b, sizeof b, 0); if (n < (ssize_t)sizeof(struct kern_event_msg)) continue;
        struct kern_event_msg *m = (void *)b; unsigned dl = m->total_size - KEV_MSG_HEADER_SIZE;
        struct timeval tv; gettimeofday(&tv, 0);
        printf("%ld.%03d sub=%u code=%u len=%u :", (long)tv.tv_sec % 1000, (int)(tv.tv_usec / 1000), m->kev_subclass, m->event_code, dl);
        for (unsigned i = 0; i < dl && i < 64; i++) printf("%s%02x", i % 4 ? "" : " ", ((unsigned char *)m->event_data)[i]);
        printf("\n");
    }
}
