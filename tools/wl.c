// Calls the private Apple80211 API from a chosen IO80211 library, in this process:
// wl [library path] [SSID]  -> scan, then (with an SSID) a join attempt with a wrong passphrase.
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "/System/Library/PrivateFrameworks/IO80211.framework/IO80211";
    void *h = dlopen(lib, RTLD_NOW);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    int (*Open)(void **) = dlsym(h, "Apple80211Open");
    int (*Bind)(void *, CFStringRef) = dlsym(h, "Apple80211BindToInterface");
    int (*Scan)(void *, CFArrayRef *, CFDictionaryRef) = dlsym(h, "Apple80211Scan");
    if (!Open || !Bind || !Scan) { printf("missing symbol\n"); return 1; }
    void *ref = 0; int e = Open(&ref); printf("open %d\n", e);
    e = Bind(ref, CFSTR("en1")); printf("bind %d\n", e);
    CFMutableDictionaryRef p = CFDictionaryCreateMutable(0, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef res = 0; e = Scan(ref, &res, p);
    printf("scan %d, networks %ld\n", e, res ? (long)CFArrayGetCount(res) : -1L);
    for (CFIndex i = 0; res && i < CFArrayGetCount(res); i++) {
        CFDictionaryRef d = CFArrayGetValueAtIndex(res, i);
        CFNumberRef rssi = CFDictionaryGetValue(d, CFSTR("RSSI")), ch = CFDictionaryGetValue(d, CFSTR("CHANNEL"));
        CFStringRef ssid = CFDictionaryGetValue(d, CFSTR("SSID_STR"));
        int r = 0, c = 0; if (rssi) CFNumberGetValue(rssi, kCFNumberIntType, &r); if (ch) CFNumberGetValue(ch, kCFNumberIntType, &c);
        printf("  net %ld: rssi %d channel %d ssid_len %ld keys %ld\n", (long)i, r, c, ssid ? (long)CFStringGetLength(ssid) : -1L, (long)CFDictionaryGetCount(d));
    }
    if (argc > 2 && res) {                       // join test: argv[2] = SSID, with a deliberately wrong passphrase
        int (*Assoc)(void *, CFDictionaryRef, CFTypeRef) = dlsym(h, "Apple80211Associate");
        CFStringRef want = CFStringCreateWithCString(0, argv[2], kCFStringEncodingUTF8);
        for (CFIndex i = 0; i < CFArrayGetCount(res); i++) {
            CFDictionaryRef d = CFArrayGetValueAtIndex(res, i);
            CFStringRef ssid = CFDictionaryGetValue(d, CFSTR("SSID_STR"));
            if (ssid && CFEqual(ssid, want)) {
                e = Assoc(ref, d, CFSTR("wrong-pass-test-0000"));
                printf("associate (wrong passphrase on purpose) -> %d\n", e);
                break;
            }
        }
    }
    return 0;
}
