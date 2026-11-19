WiFiTahoeBridge Changelog
=========================

#### v0.3.0
- AWDL / AirDrop: an `IO80211APIUserClient` for the AWDL interface (created from a hook on
  `IO80211VirtualInterface::newUserClient`, type 0), so Tahoe's `wifip2pd` can create its AWDL interface
- Requests (selector 0) go to the legacy family through `ifnet_ioctl()`; capability queries (1, 5, 7), event monitor
  init/exit (3, 4), async "data available" (2), event filter (request 0x234)
- Event ring in the modern format, filled from a hook on `IO80211VirtualInterface::IO80211InterfacePostMessage`
- The client is handed only to `wifip2pd`: with `airportd` holding one too, Wi-Fi auto-join failed after boot
- On by default; `-wtbawdloff`, safe mode and installer/recovery turn it off; `sysctl debug.wtb_awdl` switches it at
  run time; counters `debug.wtb_awdl_opens/requests/events/drops/lasterr`
- Docs: the Bluetooth setting AirDrop needs on BCM20702 controllers (`tools/bt-single-advertising.sh`)

#### v0.2.0
- First public release
- Hooks `IO80211Interface::performCommand` in IO80211FamilyLegacy (the ungated entry, in process context)
- Scan results: Tahoe's 0x8e0-byte request served from the legacy 0x48c-byte one, waits while the scan is running
- Association: Tahoe's 908-byte request converted to the legacy 592-byte layout
- Removed `OSBundleCompatibleVersion`, which made the kernel drop v0.1 silently

#### v0.1.0
- Internal test build, never loaded
