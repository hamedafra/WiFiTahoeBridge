// Interposer: logs every Apple80211 ioctl (request and reply bytes) made by this process.
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
struct a80211req { char ifname[16]; int type; int val; unsigned len; void *data; };
static FILE *out;
// Open the log before main(): a sandboxed process cannot open it later.
__attribute__((constructor)) static void tap_init(void) { const char *f = getenv("TAP_OUT"); out = f ? fopen(f, "w") : stderr; if (!out) out = stderr; }
static void hex(const char *tag, const unsigned char *p, unsigned n) {
    fprintf(out, "%s %u ", tag, n);
    for (unsigned i = 0; i < n; i++) fprintf(out, "%02x", p[i]);
    fprintf(out, "\n");
}
int tap_ioctl(int fd, unsigned long req, ...) {
    va_list ap; va_start(ap, req); void *arg = va_arg(ap, void *); va_end(ap);
    unsigned nr = req & 0xff, grp = (req >> 8) & 0xff;
    if (grp != 0x69 || (nr != 200 && nr != 201)) return ioctl(fd, req, arg);
    if (!out) { const char *f = getenv("TAP_OUT"); out = f ? fopen(f, "w") : stderr; }
    struct a80211req *r = arg;
    unsigned n = r->len > 0x8000 ? 0x8000 : r->len;
    fprintf(out, "REQ %s type=%d val=%d len=%u if=%.16s\n", nr == 200 ? "SET" : "GET", r->type, r->val, r->len, r->ifname);
    if (r->data && n) hex("IN", r->data, n);
    int ret;
    if (getenv("TAP_SHIM") && nr == 201 && r->type == 11 && r->len == 0x8e0) {
        // Tahoe asks for a 0x8e0-byte scan result; the legacy driver only serves 0x48c.
        r->len = 0x48c;
        for (int i = 0; i < 60; i++) {           // wait while the driver says the scan is still running
            ret = ioctl(fd, req, arg);
            if (!(ret && errno == 16)) break;
            usleep(200000);
        }
        r->len = 0x8e0;
        if (ret == 0) memset((char *)r->data + 0x48c, 0, 0x8e0 - 0x48c);
    } else if (getenv("TAP_SHIM") && nr == 200 && r->type == 20 && r->len == 908) {
        // Tahoe association request: two 16-bit fields became 32-bit (+4 bytes), plus a new tail.
        unsigned char v[592]; const unsigned char *t = r->data; void *keep = r->data;
        memcpy(v, t, 4); memcpy(v + 4, t + 4, 2); memcpy(v + 6, t + 8, 2); memcpy(v + 8, t + 12, 584);
        r->data = v; r->len = 592;
        ret = ioctl(fd, req, arg);
        r->data = keep; r->len = 908;
    } else
        ret = ioctl(fd, req, arg);
    fprintf(out, "RET %d errno=%d len=%u\n", ret, ret ? errno : 0, r->len);
    if (nr == 201 && ret == 0 && r->data && n) hex("OUT", r->data, n);
    fflush(out);
    return ret;
}
__attribute__((used, section("__DATA,__interpose"))) static struct { void *n, *o; } tup = { (void *)tap_ioctl, (void *)ioctl };

// TAP_NOSANDBOX=1: let a daemon copy run without entering its sandbox, so it can open the same IOKit clients.
extern int sandbox_init_with_parameters(const char *, unsigned long long, const char *const [], char **);
int nosandbox(const char *p, unsigned long long f, const char *const a[], char **e) {
    if (getenv("TAP_NOSANDBOX")) { if (e) *e = 0; return 0; }
    return sandbox_init_with_parameters(p, f, a, e);
}
__attribute__((used, section("__DATA,__interpose"))) static struct { void *n, *o; } tup2 = { (void *)nosandbox, (void *)sandbox_init_with_parameters };

// TAP_FAKEPREFS=1: pretend SCPreferences locking and committing work, so an unentitled daemon copy keeps going
// (nothing is written).
#include <SystemConfiguration/SystemConfiguration.h>
static Boolean fake_lock(SCPreferencesRef p, Boolean w) { return getenv("TAP_FAKEPREFS") ? 1 : SCPreferencesLock(p, w); }
static Boolean fake_unlock(SCPreferencesRef p) { return getenv("TAP_FAKEPREFS") ? 1 : SCPreferencesUnlock(p); }
static Boolean fake_commit(SCPreferencesRef p) { return getenv("TAP_FAKEPREFS") ? 1 : SCPreferencesCommitChanges(p); }
static Boolean fake_apply(SCPreferencesRef p) { return getenv("TAP_FAKEPREFS") ? 1 : SCPreferencesApplyChanges(p); }
#define IPOSE(n, o) __attribute__((used, section("__DATA,__interpose"))) static struct { void *a, *b; } ip_##n = { (void *)n, (void *)o };
IPOSE(fake_lock, SCPreferencesLock) IPOSE(fake_unlock, SCPreferencesUnlock) IPOSE(fake_commit, SCPreferencesCommitChanges) IPOSE(fake_apply, SCPreferencesApplyChanges)
