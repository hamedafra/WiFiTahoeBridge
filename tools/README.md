# User-space test tools

For finding out what a macOS version's IO80211 library and daemons send to the driver, without rebooting. Run them on
the hackintosh itself. Interface names (`en1`, `awdl0`) are hard-coded in places; change them if yours differ.

```bash
clang -dynamiclib -o tap.dylib tap.c -framework SystemConfiguration -framework CoreFoundation
clang -dynamiclib -o iouc.dylib iouc.c -framework IOKit -framework CoreFoundation -framework Security
clang -o wl wl.c -framework CoreFoundation
clang -fblocks -o p2pmon p2pmon.c -framework CoreFoundation -framework IOKit
clang -o raw raw.c
clang -o kevmon kevmon.c
clang -o a80211get a80211get.c
clang -o uc4 uc4.c -framework IOKit -framework CoreFoundation
swiftc -o scan scan.swift
```

## Wi-Fi requests

| Command | What it does |
|---|---|
| `TAP_OUT=$PWD/a.tap DYLD_INSERT_LIBRARIES=$PWD/tap.dylib ./wl` | Scan with the system's own IO80211 library and log every Apple80211 ioctl |
| `... ./wl /path/to/IO80211Old.dylib` | Same with another library, for example Ventura's |
| `TAP_SHIM=1 TAP_OUT=... DYLD_INSERT_LIBRARIES=... ./wl` | Apply the kext's Wi-Fi translations in user space |
| `... ./wl /System/Library/PrivateFrameworks/IO80211.framework/IO80211 "Network name"` | After the scan, try to join that network with a deliberately wrong passphrase |
| `./raw 0x1598 1 0x48c` | Raw scan: request size, UUID field on/off, result buffer size |
| `./a80211get awdl0 134 24 30` | Raw get of one request type, repeated for 30 s, printing changes (134 = AWDL sync state) |
| `./scan` | CoreWLAN scan, the same path the Wi-Fi menu uses |

A tap file has `REQ` (direction, type, length, interface), `IN` (request bytes), `RET` (return value, errno) and
`OUT` (reply bytes) lines. Taps contain network names and other details of your surroundings, so look before you
share one.

## AWDL user client

`iouc.c` is a user-space model of the kernel side of the modern API user client (see `docs/awdl.md`): it
interposes the IOKit calls of a process and, with `IOUC_EMU=1`, answers `IOServiceOpen(type 0)` on the AWDL
interface itself, passing requests to the legacy family through the socket ioctl and filling an event ring from the
kernel-event socket. It is what proved the design before the kernel version was written.

| Command | What it does |
|---|---|
| `IOUC_LOG=/dev/stdout DYLD_INSERT_LIBRARIES=$PWD/iouc.dylib ./p2pmon` | Bind to awdl0 like wifip2pd and start P2P event monitoring; without emulation this fails like the real daemon (-3900) |
| `IOUC_EMU=1 IOUC_LOG=... DYLD_INSERT_LIBRARIES=... ./p2pmon` | Same with the user-space user client: init returns 0 |
| `./kevmon 30` | Print every 802.11 kernel event (vendor Apple, class 6) for 30 s |
| `./uc4 awdl0 4` | Try to open the legacy event user client (needs an entitlement; shows the error) |

To run the real Tahoe `wifip2pd` on the model (no kext), as root and only for testing: copy
`/usr/libexec/wifip2pd` to `/var/tmp/wtb/p2pd` (x86_64 slice, `codesign -f -s -`), put `tap.dylib` and
`iouc.dylib` next to it, `launchctl bootout system/com.apple.wifip2pd`, then
`launchctl bootstrap system dev.wtb.wifip2pd.plist` (the job registers `com.apple.wifip2pd` so other daemons reach
it; `TAP_FAKEPREFS=1` lets the unentitled copy get past its preferences lock without writing anything). A reboot
restores the system daemon. The model cannot receive AWDL events (the legacy family only hands them to an
entitled client), which is why the kext exists.

## Bluetooth

`bt-single-advertising.sh on|off|status`: the `bluetoothd` setting AirDrop needs on BCM20702 controllers (see the
main README).
