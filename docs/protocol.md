# Tahoe user space vs. IO80211FamilyLegacy: what differs

Measured on macOS 26.7.3 (25G342), x86_64, BCM4360, with the tools in `tools/`. "Legacy" is the Ventura-era
`IO80211FamilyLegacy` 1200.12.2b1 injected by OpenCore; "Ventura library" is the `IO80211Old.dylib` that OpenCore
Legacy Patcher installs with its root patch.

## Transport

Both libraries open a datagram socket and issue

```c
struct apple80211req {
    char     ifname[16];
    uint32_t type;      // APPLE80211_IOC_*
    int32_t  val;
    uint32_t len;       // size of the buffer at data
    uint32_t pad;
    uint64_t data;      // user address
};
#define SIOCSA80211 _IOW('i', 200, struct apple80211req)   // set
#define SIOCGA80211 _IOWR('i', 201, struct apple80211req)  // get
```

In the kernel the request arrives at `IO80211Interface::performCommand(IONetworkController *, unsigned long cmd,
void *, void *req)`, which runs `performGatedCommand` through the command gate. The low 16 bits of `cmd` are 0x69c8
(set) or 0x69c9 (get).

## Scan request: type 10, set

| | Size |
|---|---|
| Ventura library | 0x14e0 or 0x1550 |
| Tahoe library | 0x1598 |

The legacy family accepts the Tahoe size. It can return errno 6 or 16 for a moment when the interface is busy;
both libraries retry.

## Scan result: type 11, get

| | Size |
|---|---|
| Ventura library | 0x48c |
| Tahoe library | 0x8e0 |

Legacy `getScanResult` checks `len == 0x48c` and fails otherwise. The fixed header is identical in both layouts and
the IE data starts at 0x8c in both; Tahoe's struct has a longer IE area and one new field at 0x8d0. Serving the
request with 0x48c and zeroing bytes 0x48c..0x8e0 gives the Tahoe library what it expects.

Timing differs as well. The Ventura library waits for the scan-done event before it reads results. The Tahoe
library reads at once, and the legacy family answers `EBUSY` while the scan is running. The bridge retries every
200 ms for up to 15 s.

## Association: type 20, set

| | Size |
|---|---|
| Ventura library | 592 |
| Tahoe library | 908 |

Two 16-bit fields near the start became 32-bit (4 more bytes), and a new tail follows the old content:

```
legacy[0:4]   = tahoe[0:4]
legacy[4:6]   = tahoe[4:6]
legacy[6:8]   = tahoe[8:10]
legacy[8:592] = tahoe[12:596]
```

With this conversion the request the Tahoe library sends is byte-identical to the one the Ventura library sends for
the same network and passphrase, and the legacy family answers both the same way.

## Seen but not translated

| Request | Tahoe | Result | Effect |
|---|---|---|---|
| 207 `CHANNELS_INFO`, get | len 10016 | -3900 (unsupported) | none noticed |
| 1 SSID, 9 BSSID, get | 32, 6 | -3903 while not associated | expected |

Not examined: the `wifip2pd` / AWDL path used by AirDrop and Handoff.
