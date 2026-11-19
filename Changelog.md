WiFiTahoeBridge Changelog
=========================

#### v0.2.0
- First public release
- Hooks `IO80211Interface::performCommand` in IO80211FamilyLegacy (the ungated entry, in process context)
- Scan results: Tahoe's 0x8e0-byte request served from the legacy 0x48c-byte one, waits while the scan is running
- Association: Tahoe's 908-byte request converted to the legacy 592-byte layout
- Removed `OSBundleCompatibleVersion`, which made the kernel drop v0.1 silently

#### v0.1.0
- Internal test build, never loaded
