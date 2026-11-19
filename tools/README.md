# User-space test tools

For finding out what a macOS version's IO80211 library sends to the driver, without sudo and without rebooting.
Run them on the hackintosh itself. The interface name `en1` is hard-coded; change it if yours differs.

```bash
clang -dynamiclib -o tap.dylib tap.c
clang -o wl wl.c -framework CoreFoundation
clang -o raw raw.c
swiftc -o scan scan.swift
```

| Command | What it does |
|---|---|
| `TAP_OUT=$PWD/a.tap DYLD_INSERT_LIBRARIES=$PWD/tap.dylib ./wl` | Scan with the system's own IO80211 library and log every Apple80211 ioctl |
| `... ./wl /path/to/IO80211Old.dylib` | Same with another library, for example Ventura's |
| `TAP_SHIM=1 TAP_OUT=... DYLD_INSERT_LIBRARIES=... ./wl` | Apply the bridge's translations in user space |
| `... ./wl /System/Library/PrivateFrameworks/IO80211.framework/IO80211 "Network name"` | After the scan, try to join that network with a deliberately wrong passphrase. Both libraries should fail the same way |
| `./raw 0x1598 1 0x48c` | Raw scan: request size, UUID field on/off, result buffer size |
| `./scan` | CoreWLAN scan, the same path the Wi-Fi menu uses |

A tap file has `REQ` (direction, type, length), `IN` (request bytes), `RET` (return value, errno) and `OUT` (reply
bytes) lines. Taps contain network names and other details of your surroundings, so look before you share one.
