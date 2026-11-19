// User-space stand-in for the kernel side of Tahoe's Wi-Fi "API user client" (IO80211APIUserClient), backed by the
// legacy family's socket ioctls and kernel events. For testing the real IO80211.framework / wifip2pd client code
// without a kext or a reboot. Load with DYLD_INSERT_LIBRARIES.
//   IOUC_LOG=file   log   IOUC_EMU=1 emulate   IOUC_INFRA=1 also emulate on the infrastructure interface
//   IOUC_SEL1/5/7   answers of the three 1-byte queries (defaults 1, 1, 0)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <Security/SecTask.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
#include <sys/ioctl.h>
#include <sys/kern_event.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>

#define FAKE_BASE 0x57420000u
#define RINGSZ 0x4b000u
#define STATESZ 0x1000u
struct a80211req { char ifname[16]; int type; int val; unsigned len; unsigned pad; void *data; };
struct conn { int used, subscribed, monitoring, maskset; char ifname[16]; uint8_t *ring; uint32_t *state; uint64_t aref[8];
              mach_port_t wake; uint8_t mask[32]; uint16_t seq; unsigned pushed, dropped; };
static struct conn conns[8];
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
static FILE *out; static int sock = -1, kev = -1;

__attribute__((constructor)) static void init(void) {
    const char *f = getenv("IOUC_LOG"); out = f ? fopen(f, "w") : stderr; if (!out) out = stderr; setvbuf(out, 0, _IOLBF, 0);
    sock = socket(AF_INET, SOCK_DGRAM, 0);             // before the process sandboxes itself
    kev = socket(PF_SYSTEM, SOCK_RAW, SYSPROTO_EVENT);
}
static void rows(const char *tag, const void *v, size_t n, size_t max) {
    const unsigned char *p = v; if (!p) return; if (n > max) n = max;
    for (size_t o = 0; o < n; o += 16) {
        int nz = 0; for (size_t i = o; i < o + 16 && i < n; i++) nz |= p[i];
        if (!nz) continue;
        fprintf(out, "  %s +0x%04zx:", tag, o); for (size_t i = o; i < o + 16 && i < n; i++) fprintf(out, " %02x", p[i]); fprintf(out, "\n");
    }
}
static int emu(void) { return getenv("IOUC_EMU") != 0; }
static int answer(const char *name, int def) { const char *v = getenv(name); return v ? atoi(v) : def; }
static struct conn *get(io_connect_t c) { unsigned i = c - FAKE_BASE - 1; return (c & 0xffffff00u) == FAKE_BASE && i < 8 && conns[i].used ? &conns[i] : 0; }

// ---- event ring (format of IO80211APIUserClient's event pipe) ----
static void notify(struct conn *k) {
    struct { mach_msg_header_t h; uint32_t size, type; uint64_t ref[8]; int32_t result; } __attribute__((packed)) m;
    if (!k->subscribed) return;
    memset(&m, 0, sizeof m);
    m.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MAKE_SEND, 0); m.h.msgh_size = sizeof m; m.h.msgh_remote_port = k->wake; m.h.msgh_id = 53;
    m.size = sizeof m - sizeof m.h; m.type = 150; memcpy(m.ref, k->aref, sizeof m.ref);
    kern_return_t r = mach_msg(&m.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof m, 0, MACH_PORT_NULL, 100, MACH_PORT_NULL);
    if (r) fprintf(out, "  notify failed 0x%x\n", r);
}
static void push(struct conn *k, uint32_t msg, const void *data, unsigned len) {
    if (!k->ring || !k->monitoring) return;
    if (k->maskset && !k->mask[0] && !(msg >= 1 && msg < 249 && (k->mask[1 + (msg - 1) / 8] >> ((msg - 1) % 8) & 1))) return;
    pthread_mutex_lock(&lk);
    uint32_t r = k->state[0], w = k->state[1], pad = (4 - len % 4) % 4, need = 0x30 + len + pad, fill = 0;
    if (RINGSZ - w < 0x30) fill = RINGSZ - w;
    uint32_t freeb = (r + RINGSZ - w - 4) % RINGSZ; if (r == w) freeb = RINGSZ - 4;
    if (len > 0x1000 || need + fill > freeb) { k->dropped++; pthread_mutex_unlock(&lk); notify(k); return; }
    for (uint32_t i = 0; i < fill; i += 4) *(uint32_t *)(k->ring + w + i) = 0x50;
    if (fill) w = 0;
    uint8_t h[0x30] = {0}; struct timeval tv; gettimeofday(&tv, 0);
    *(uint16_t *)(h + 6) = ++k->seq; *(uint64_t *)(h + 8) = tv.tv_sec; *(uint64_t *)(h + 16) = tv.tv_usec;
    *(uint16_t *)(h + 0x18) = len; *(uint16_t *)(h + 0x1a) = pad; *(uint32_t *)(h + 0x1c) = msg; strlcpy((char *)h + 0x20, k->ifname, 16);
    memcpy(k->ring + w, h, 0x30);
    for (unsigned i = 0; i < len; i++) k->ring[(w + 0x30 + i) % RINGSZ] = ((const uint8_t *)data)[i];
    *(uint32_t *)(k->ring + w) = 0x57;
    k->state[1] = (w + need) % RINGSZ; k->pushed++;
    pthread_mutex_unlock(&lk);
    notify(k);
}
// ---- event source: the classic kernel-event socket the legacy family posts to ----
static void *kevloop(void *x) {
    struct kev_request f = { KEV_VENDOR_APPLE, 6 /* KEV_IEEE80211_CLASS */, KEV_ANY_SUBCLASS };
    if (kev < 0 || ioctl(kev, SIOCSKEVFILT, &f)) { fprintf(out, "kev: unavailable (errno %d)\n", errno); return 0; }
    static uint8_t buf[0x2000];
    for (;;) {
        ssize_t n = recv(kev, buf, sizeof buf, 0); if (n < (ssize_t)sizeof(struct kern_event_msg)) continue;
        struct kern_event_msg *m = (void *)buf; unsigned dl = m->total_size - KEV_MSG_HEADER_SIZE;
        if (dl < 24) { fprintf(out, "kev: msg=%u subclass=%u datalen=%u\n", m->event_code, m->kev_subclass, dl); rows("kev", m->event_data, dl, 64);
            char ifs[17] = {0}; memcpy(ifs, m->event_data, dl < 16 ? dl : 16);        // legacy posts just the interface name
            for (int i = 0; i < 8; i++) if (conns[i].used && !strcmp(conns[i].ifname, ifs)) push(&conns[i], m->event_code, (uint8_t *)m->event_data + (dl < 16 ? dl : 16), dl > 16 ? dl - 16 : 0);
            continue; }
        struct net_event_data *ne = (void *)m->event_data; char ifn[32]; snprintf(ifn, sizeof ifn, "%.16s%u", ne->if_name, ne->if_unit);
        fprintf(out, "kev: if=%s msg=%u len=%u\n", ifn, m->event_code, dl - 24);
        for (int i = 0; i < 8; i++) if (conns[i].used && !strcmp(conns[i].ifname, ifn)) push(&conns[i], m->event_code, (uint8_t *)m->event_data + 24, dl - 24);
    }
}
// ---- interposed IOKit calls ----
static CFTypeRef my_ent(SecTaskRef t, CFStringRef key, CFErrorRef *e) {
    if (emu() && CFEqual(key, CFSTR("com.apple.private.driverkit.driver-access"))) {
        CFStringRef v = CFSTR("com.apple.private.wifi.driverkit"); return CFArrayCreate(0, (const void **)&v, 1, &kCFTypeArrayCallBacks);
    }
    return SecTaskCopyValueForEntitlement(t, key, e);
}
static kern_return_t my_open(io_service_t s, task_port_t task, uint32_t type, io_connect_t *c) {
    io_name_t cls = ""; IOObjectGetClass(s, cls);
    kern_return_t r = IOServiceOpen(s, task, type, c);
    if (type == 0 || getenv("IOUC_LOGALL")) fprintf(out, "IOServiceOpen class=%s type=%u -> 0x%x\n", cls, type, r);
    if (r && emu() && type == 0 && (strstr(cls, "P2P") || strstr(cls, "Virtual") || getenv("IOUC_INFRA")))
        for (int i = 0; i < 8; i++) if (!conns[i].used) {
            memset(&conns[i], 0, sizeof conns[i]); conns[i].used = 1; *c = FAKE_BASE + 1 + i;
            fprintf(out, "  emulated: conn=0x%x\n", *c); return 0;
        }
    return r;
}
static kern_return_t my_close(io_connect_t c) {
    struct conn *k = get(c); if (!k) return IOServiceClose(c);
    fprintf(out, "[%s] close (pushed %u dropped %u)\n", k->ifname, k->pushed, k->dropped); k->used = 0; return 0;
}
static kern_return_t my_struct(io_connect_t c, uint32_t sel, const void *in, size_t inl, void *o, size_t *ol) {
    struct conn *k = get(c);
    if (!k) return IOConnectCallStructMethod(c, sel, in, inl, o, ol);
    if ((sel == 0 && inl >= 0x3c30)) {
        uint32_t cmd = *(const uint32_t *)in, dlen = *(const uint32_t *)((const char *)in + 4);
        struct a80211req r = *(const struct a80211req *)((const char *)in + 0x3c08);
        int set = (cmd | 0x80000) == 0x802869c8;
        if (!k->ifname[0]) strlcpy(k->ifname, r.ifname, 16);
        if (set && r.type == 0x234) {                    // event filter: handled by the user client itself
            memcpy(k->mask, (const char *)in + 8, dlen < 32 ? dlen : 32); k->maskset = 1;
            fprintf(out, "[%s] SET 564 event filter: all=%u\n", r.ifname, k->mask[0]); rows("mask", k->mask, 32, 32);
            if (ol) *ol = 0; return 0;
        }
        if (set) { r.data = (void *)((const char *)in + 8); r.len = dlen; }
        else { r.data = o; r.len = ol ? (unsigned)*ol : 0; if (o && ol) memset(o, 0, *ol); }
        int e = ioctl(sock, set ? 0x802869c8UL : 0xc02869c9UL, &r), en = e ? errno : 0;
        fprintf(out, "[%s] %s %d val=%d len=%u -> %s%d retlen=%u\n", r.ifname, set ? "SET" : "GET", r.type, r.val, set ? dlen : (ol ? (unsigned)*ol : 0), en ? "errno " : "ok ", en, r.len);
        if (set) rows("in", r.data, dlen, 128); else if (!en) rows("out", o, r.len, 64);
        if (!set && ol && !en && r.len <= *ol) *ol = r.len;
        if (set && ol) *ol = 0;
        return en;                                       // the real client returns the handler's errno untranslated
    }
    fprintf(out, "[%s] sel=%u in=%zu out=%zu", k->ifname, sel, inl, ol ? *ol : 0);
    if (ol && *ol == 1 && (sel == 1 || sel == 5 || sel == 7)) {
        char n[16]; snprintf(n, sizeof n, "IOUC_SEL%u", sel);
        *(unsigned char *)o = (unsigned char)answer(n, sel == 7 ? 0 : 1); fprintf(out, " -> %u\n", *(unsigned char *)o); return 0;
    }
    if (sel == 3) {
        static pthread_t th; static int started;
        if (!k->ring) { k->ring = mmap(0, RINGSZ, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); k->state = mmap(0, STATESZ, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0); }
        k->monitoring = 1; if (!started) { started = 1; pthread_create(&th, 0, kevloop, 0); }
        fprintf(out, " event monitor init\n"); return 0;
    }
    if (sel == 4) { k->monitoring = 0; k->subscribed = 0; fprintf(out, " event monitor exit\n"); return 0; }
    fprintf(out, " -> unsupported\n"); rows("in", in, inl, 64);
    return kIOReturnUnsupported;
}
static kern_return_t my_async(io_connect_t c, uint32_t sel, mach_port_t wake, uint64_t *ref, uint32_t refc, const uint64_t *in, uint32_t inc,
                              const void *si, size_t sil, uint64_t *o, uint32_t *oc, void *so, size_t *sol) {
    struct conn *k = get(c);
    if (!k) return IOConnectCallAsyncMethod(c, sel, wake, ref, refc, in, inc, si, sil, o, oc, so, sol);
    fprintf(out, "[%s] async sel=%u wake=0x%x refc=%u\n", k->ifname, sel, wake, refc);
    if (sel != 2) return kIOReturnUnsupported;
    memset(k->aref, 0, sizeof k->aref); memcpy(k->aref, ref, (refc < 8 ? refc : 8) * 8); k->wake = wake; k->subscribed = 1;
    return 0;
}
static kern_return_t my_map(io_connect_t c, uint32_t type, task_port_t task, mach_vm_address_t *a, mach_vm_size_t *sz, IOOptionBits opt) {
    struct conn *k = get(c);
    if (!k) return IOConnectMapMemory(c, type, task, a, sz, opt);
    if ((type != 1 && type != 2) || !k->ring) { fprintf(out, "[%s] map type=%u -> error\n", k->ifname, type); return 0xe0000001; }
    *a = (mach_vm_address_t)(type == 1 ? (void *)k->ring : (void *)k->state); *sz = type == 1 ? RINGSZ : STATESZ;
    fprintf(out, "[%s] map type=%u opt=0x%x size=0x%llx\n", k->ifname, type, opt, *sz);
    return 0;
}
static kern_return_t my_unmap(io_connect_t c, uint32_t type, task_port_t task, mach_vm_address_t a) {
    if (!get(c)) return IOConnectUnmapMemory(c, type, task, a);
    return 0;
}
#define IPOSE(n, o) __attribute__((used, section("__DATA,__interpose"))) static struct { void *a, *b; } ip_##n = { (void *)n, (void *)o };
IPOSE(my_ent, SecTaskCopyValueForEntitlement) IPOSE(my_open, IOServiceOpen) IPOSE(my_close, IOServiceClose)
IPOSE(my_struct, IOConnectCallStructMethod) IPOSE(my_async, IOConnectCallAsyncMethod) IPOSE(my_map, IOConnectMapMemory) IPOSE(my_unmap, IOConnectUnmapMemory)
