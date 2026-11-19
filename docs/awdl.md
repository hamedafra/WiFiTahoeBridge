# AWDL on Tahoe over IO80211FamilyLegacy

Measured on macOS 26.7.3 (25G342), x86_64, BCM4360 + IO80211FamilyLegacy 1200.12.2b1, with the tools in `tools/`.
Interface descriptions come from observing the stock binaries' behaviour and their public symbol names.

## Why AirDrop fails without help

Tahoe's `wifip2pd` (WiFiP2P-861.4) binds to each Wi-Fi interface with `Apple80211BindToInterfaceWithService`,
which first does `IOServiceOpen(interface service, task, type 0)`. On the legacy family that returns
`0xe00002c2`: `IO80211Interface::newUserClient` accepts only type 2 and `IO80211VirtualInterface::newUserClient`
only types 4 and 5, both for the older `IO80211AsyncEventUserClient`, which also requires an entitlement
(`com.apple.wifi_awdl.event_monitor` for AWDL) that Tahoe's software never asks for.

For requests the library falls back to socket ioctls, which is why plain Wi-Fi works. For events it does not:
`Apple80211RawP2PEventMonitoringInit` uses only the user-client event pipe, so `wifip2pd` logs
`Unable to create AWDLInterface for awdl0 because initialization error: Invalid Parameters`, and AirDrop never
starts. The legacy family never posts AWDL events to the kernel-event socket either.

## The user client Tahoe expects (as used by IO80211.framework)

| Call | Meaning |
|---|---|
| `IOServiceOpen(service, task, 0)` | open; the library first checks its own entitlement `com.apple.private.driverkit.driver-access` = `com.apple.private.wifi.driverkit` |
| struct method 0, input 0x3c30 bytes | Apple80211 request: `u32 cmd` @0 (`0xc02869c9` get, `0x802869c8` set), `u32 len` @4, payload @8 (max 0x3c00), `apple80211req` @0x3c08 (ifname, type @+0x10, val @+0x14). Get: output = the raw payload, output size = returned length. Result = the handler's errno, untranslated |
| struct method 1, 1-byte output | "use the user client when possible" (1) |
| struct method 7, 1-byte output | "DriverKit supported" (0) |
| struct method 5, 1-byte output | "use the event pipe" (1; 0 makes P2P event monitoring fail with -3903) |
| struct method 3, no arguments | start event monitoring (allocates the ring) |
| struct method 4, no arguments | stop event monitoring |
| `IOConnectMapMemory` type 1 (options 0x1001), type 2 (options 0x1) | event ring, ring state |
| async method 2, 8 reference words | "data available" notification |
| request type 0x234 (set, 32 bytes) | event filter: byte 0 = all, then a bitmap of event codes |
| struct method 6 (0x3c030-byte input) | large-payload variant of method 0; not seen in use |

Ring state: `u32 read` @0 (written by the client), `u32 write` @4 (written by the kernel). Ring records: a 0x30-byte
header `{u32 marker; u16 0; u16 seq; u64 sec; u64 usec; u16 len; u16 pad; u32 event; char ifname[16]}` followed by
the payload, 4-byte aligned. Marker 0x57 = complete record; 0x50 = filler word (used when fewer than 0x30 bytes are
left before the end, headers never wrap); the payload may wrap. Empty when read == write; 4 bytes always kept free.
The client reads until it meets a marker that is not 0x57.

## What the kext does

- `IO80211VirtualInterface::newUserClient` hook: type 0 on an interface named `awdl*` gets a
  `WiFiTahoeBridge`-built `IO80211APIUserClient` (that exact class name is in `wifip2pd`'s
  `com.apple.security.iokit-user-client-class` entitlement, which its sandbox enforces). Everything else goes to
  the original function. Only a root process named `wifip2pd` gets it. `airportd` also calls
  `Apple80211BindToInterfaceWithService` on awdl0; when it got the client too, it listed awdl0 as an infrastructure
  interface ("role=undefined/infra, parent=en1") and every auto-join attempt on en1 ended with
  `error=(37 'driver not available')`, so it is left on the ioctl path.
- Requests: copied into a kernel buffer that is also mapped into the client task, then
  `ifnet_ioctl(awdl0, SIOCSA80211/SIOCGA80211)` with that user address, the same path a socket ioctl takes.
- Events: `IO80211VirtualInterface::IO80211InterfacePostMessage(this, code, data, len)` hook copies every event of
  the interface into the ring of each client opened on it, then calls the original. Events over 0x1000 bytes and
  events that do not fit are counted as drops.
- Notification: `sendAsyncResult64WithOptions(..., kIOUserNotifyOptionCanDrop)` after each event.

## Requests seen from Tahoe's wifip2pd on awdl0

All accepted by the legacy family unchanged, except 575:

| Type | Name (from public Apple80211 headers) | Direction | Size |
|---|---|---|---|
| 12 | CARD_CAPABILITIES | get | 21 (12 returned) |
| 43 | DRIVER_VERSION | get | 0x180 |
| 97 | VIRTUAL_IF_PARENT | get | 16 |
| 117 | AWDL_SYNC_ENABLED | get | 12 |
| 120 | AWDL_PEER_SERVICE_REQUEST | set | 20, 41 (a DNS question) |
| 148 | AWDL_STRATEGY | get | 24 |
| 244 | AWDL_PREFERRED_CHANNELS | get | 12 |
| 293 | (companion interface, payload names `llw0`) | set | 24 |
| 575 | OS_ELIGIBILITY | set | 4 -> errno 102 (unsupported) |

On en1 Tahoe also sends set 389 (USERSPACE_P2P_OPTIONS), set 566 (HAS_BATTERY), set 450 (GATEWAY_ADDRESS) and the
"AWDL enabled/disabled" notification, all unsupported by the legacy family; nothing visible breaks.

## Result

With the kext, the stock `wifip2pd` logs `Created AWDLInterface`, creates `llw0`, browses and advertises
`_airdrop._tcp`, `_companion-link._tcp`, `_appsvcprepair._tcp`, discovers an iPhone and a Mac on awdl0 and
establishes data paths; `ping6` to a peer's link-local address over awdl0 works. AirDrop works once Bluetooth LE
advertising works (see the README).

## Bluetooth LE advertising (BCM20702 on Tahoe)

`bluetoothd` (25G342) treats the BCM20702 as a chipset with LE multiple advertising. At stack start
`LeBroadcaster::stackDidStart()` calls `LE_InitMultipleAdvertising`, the controller rejects it (status 0x98 = 152),
the broadcaster logs `status != OI_OK` and stays "not started", and every later request ends with
`Stack is not started, updateAdvertisement returning`. The chipset model (`LEHardwareModel`) reads a preference
override, domain `com.apple.MobileBluetooth.debug`, dictionary `LE`, key `SupportsMultipleAdvertising`; with
`false` it logs `Warning: Overriding Multiple Advertising support - disabled`, skips the multi-advertising set-up
and advertising starts.
