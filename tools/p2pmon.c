// Does what wifip2pd does first on an AWDL interface: bind, then P2P event monitoring. p2pmon [ifname] [library]
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    const char *ifn = argc > 1 ? argv[1] : "awdl0";
    const char *lib = argc > 2 ? argv[2] : "/System/Library/PrivateFrameworks/IO80211.framework/IO80211";
    void *h = dlopen(lib, RTLD_NOW); if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    int (*Open)(void **) = dlsym(h, "Apple80211Open");
    int (*Bind)(void *, CFStringRef) = dlsym(h, "Apple80211BindToInterface");
    int (*P2PInit)(void *, dispatch_queue_t, void *) = dlsym(h, "Apple80211RawP2PEventMonitoringInit");
    int (*RawGet)(void *, int, int, void *, int) = dlsym(h, "Apple80211RawGet");
    if (!Open || !Bind || !P2PInit) { printf("missing symbol\n"); return 1; }
    void *ref = 0; int e = Open(&ref); printf("open %d\n", e);
    CFStringRef name = CFStringCreateWithCString(0, ifn, kCFStringEncodingUTF8);
    int (*BindSvc)(void *, io_service_t, CFStringRef, int) = dlsym(h, "Apple80211BindToInterfaceWithService");
    io_service_t svc = 0;
    const char *keys[] = { "IOInterfaceName", "BSD Name" };
    for (int k = 0; k < 2 && !svc; k++) {
        CFStringRef key = CFStringCreateWithCString(0, keys[k], kCFStringEncodingUTF8);
        CFDictionaryRef pm = CFDictionaryCreate(0, (const void **)&key, (const void **)&name, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFStringRef top = CFSTR("IOPropertyMatch");
        CFDictionaryRef m = CFDictionaryCreate(0, (const void **)&top, (const void **)&pm, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        svc = IOServiceGetMatchingService(0, m);
        if (svc) { io_name_t c = ""; IOObjectGetClass(svc, c); printf("service by %s: class %s\n", keys[k], c); }
    }
    if (svc && BindSvc && !getenv("P2PMON_PLAINBIND")) { e = BindSvc(ref, svc, name, getenv("P2PMON_ARG4") ? atoi(getenv("P2PMON_ARG4")) : 0); printf("bind-with-service %s %d\n", ifn, e); }
    else { e = Bind(ref, name); printf("bind %s %d\n", ifn, e); }
    dispatch_queue_t q = dispatch_queue_create("p2pmon", 0);
    e = P2PInit(ref, q, ^(long a, long b, long c, long d, long f, long g) { printf("event: %ld %ld %ld %ld %ld %ld\n", a, b, c, d, f, g); });
    printf("P2P event monitoring init %d\n", e);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (getenv("P2PMON_SECS") ? atoi(getenv("P2PMON_SECS")) : 4) * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ printf("done\n"); _exit(0); });
    dispatch_main();
}
