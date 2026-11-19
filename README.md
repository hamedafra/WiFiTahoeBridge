# WiFiTahoeBridge

**Broadcom Wi-Fi (BCM4360 class) on macOS 26 Tahoe without OpenCore Legacy Patcher root patches.**

[فارسی](README.fa.md)

A small [Lilu](https://github.com/acidanthera/Lilu) plugin for hackintoshes (OpenCore). It lets the stock Wi-Fi
software of macOS Tahoe talk to the old Broadcom driver stack that OpenCore injects, so the system volume stays
sealed and untouched: no `IO80211.framework` replacement, no root patching after every update.

| | |
|---|---|
| Status | Works on the author's machine: scan, join, DHCP, normal use from the Wi-Fi menu |
| Tested on | macOS 26.7.3 (25G342), one PC, one card (BCM4360) |
| Version | 0.2.0, first public release |

## The problem it solves

Apple removed the drivers for older Broadcom cards (BCM4360, BCM43602, BCM4350, and BCM4352 as used with
AirportBrcmFixup) in macOS 14 Sonoma.
The usual fix today has two halves:

1. **Kernel half**, injected by OpenCore: `IOSkywalkFamily` (downgraded), `IO80211FamilyLegacy` and
   `AirPortBrcmNIC`. No change to the system volume.
2. **User-space half**, the *root patch* done by OpenCore Legacy Patcher ("Modern Wireless"): replace
   `IO80211.framework`, `WiFiPeerToPeer.framework` and `wifip2pd` on the system volume with Ventura's copies.

The second half is the painful one. It breaks the sealed system volume, needs SIP lowered and AMFI worked around,
and has to be applied again after every macOS update.

Without the root patch on Tahoe, the kernel half still loads and the card shows up (`en1`, firmware version, "Not
Associated"), but a scan returns nothing and you cannot join a network. The reason is small: Tahoe's
`IO80211.framework` and the Ventura-era kernel family disagree on three requests. WiFiTahoeBridge translates those
three inside the kernel, so the root patch is not needed for Wi-Fi itself.

## What works and what does not

| Feature | State |
|---|---|
| Scan, network list in the Wi-Fi menu | Works |
| Join a WPA2/WPA3 Personal network, DHCP, traffic | Works (5 GHz, 80 MHz, 802.11ac, 1053 Mbps link rate) |
| Sealed system volume, no root patch | Yes |
| AirDrop, Handoff, AirPlay to Mac, other AWDL features | **Not handled.** They go through `wifip2pd`, which this kext does not touch. Expect them not to work |
| WPA3-only, WPA Enterprise, open/WEP networks, hotspot | Not tested |
| Sleep/wake, long uptime | Not tested yet |
| Cards other than BCM4360, real Macs | Not tested |
| macOS 15 Sequoia and older | Not supported (the kext only starts on Darwin 25) |

This is a first release tested on a single machine. Keep a bootable backup of your EFI (a USB stick) before you
try it.

## Requirements

- OpenCore with a [Lilu](https://github.com/acidanthera/Lilu/releases) that supports Tahoe (tested with 1.7.2).
- The kernel half already set up for Tahoe. These kexts come from
  [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher/tree/main/payloads/Kexts/Wifi)
  (`payloads/Kexts/Wifi`); they are Apple binaries and are not included here:
  - `IOSkywalkFamily.kext`, plus a `Kernel > Block` entry for `com.apple.iokit.IOSkywalkFamily`
    (`Strategy` = `Exclude`, `MinKernel` = `23.0.0`)
  - `IO80211FamilyLegacy.kext`
  - `AirPortBrcmNIC.kext` from `AirPortBrcmNIC-Tahoe` (`MinKernel` = `25.0.0`)
- A card driven by `AirPortBrcmNIC`. Tested: BCM4360 (TP-Link Archer T9E, `14e4:43a0`).

If the card already appears in System Information > Wi-Fi on Tahoe but finds no networks, the kernel half is in
place and this kext is the missing piece.

## Install

1. Download `WiFiTahoeBridge-0.2.0-RELEASE.zip` from
   [Releases](https://github.com/hamedafra/WiFiTahoeBridge/releases) and copy `WiFiTahoeBridge.kext` to
   `EFI/OC/Kexts`.
2. Add it to `Kernel > Add` in `config.plist`, anywhere after `Lilu.kext`:

   ```xml
   <dict>
       <key>Arch</key>
       <string>x86_64</string>
       <key>BundlePath</key>
       <string>WiFiTahoeBridge.kext</string>
       <key>Comment</key>
       <string>Tahoe Wi-Fi requests to IO80211FamilyLegacy</string>
       <key>Enabled</key>
       <true/>
       <key>ExecutablePath</key>
       <string>Contents/MacOS/WiFiTahoeBridge</string>
       <key>MaxKernel</key>
       <string></string>
       <key>MinKernel</key>
       <string>25.0.0</string>
       <key>PlistPath</key>
       <string>Contents/Info.plist</string>
   </dict>
   ```

3. If the system was root patched for Wi-Fi before, revert the root patches in OpenCore Legacy Patcher first. A
   fresh Tahoe install or upgrade has none.
4. Reboot, open the Wi-Fi menu.

To check that it loaded:

```bash
kextstat | grep WiFiTahoeBridge
```

### Boot arguments

| Argument | Effect |
|---|---|
| `-wtboff` | Disable the kext |
| `-wtbdbg` | Debug logging (needs a DEBUG build) |
| `-wtbbeta` | Load on a Darwin version newer than 25 |

### The configuration it was tested with

Asus TUF Z390-Pro Gaming, i9-9900K, SMBIOS iMac20,1, OpenCore 1.0.8, Lilu 1.7.2, AirportBrcmFixup 2.2.1,
IOSkywalkFamily 1.0, IO80211FamilyLegacy 1200.12.2b1, AirPortBrcmNIC 1400.1.1.

The machine was upgraded from a root-patched Sequoia install and still has its old settings: `AMFIPass.kext`,
`csr-active-config` = `03080000`, `SecureBootModel` = `Disabled`, boot argument `-lilubetaall`. Whether the kext
works with SIP fully on and the default Secure Boot model has **not** been checked; reports are welcome.

## How it works

Tahoe's `IO80211.framework` reaches the driver with the same Apple80211 ioctls as Ventura's
(`SIOCSA80211` / `SIOCGA80211` on a socket, with an `apple80211req`). Three requests differ:

| Request | Tahoe sends | Legacy family wants | Translation |
|---|---|---|---|
| `APPLE80211_IOC_SCAN_RESULT` (11, get) | 0x8e0-byte buffer | exactly 0x48c bytes | call with 0x48c, zero the rest. The header is the same; the tail is a longer IE area and one new field |
| same request, timing | reads results right away | `EBUSY` until the scan is done | retry every 200 ms, up to 15 s |
| `APPLE80211_IOC_ASSOCIATE` (20, set) | 908-byte struct | 592-byte struct | two 16-bit fields became 32-bit and a tail was added: `v[0:4]=t[0:4]`, `v[4:6]=t[4:6]`, `v[6:8]=t[8:10]`, `v[8:592]=t[12:596]` |

`APPLE80211_IOC_SCAN_REQ` (10) is larger on Tahoe too (0x1598 bytes), but the legacy family accepts it as is.

The kext routes `IO80211Interface::performCommand` in `IO80211FamilyLegacy` and rewrites only these requests, only
when they come from a user process with exactly the Tahoe sizes. Everything else passes through unchanged. More
detail is in [docs/protocol.md](docs/protocol.md).

## How it was found: no-reboot testing in user space

Everything was measured and proven in user space first, on the running Tahoe system, without sudo or reboots. The
tools are in [tools/](tools/):

- `tap.c`: a `DYLD_INSERT_LIBRARIES` interposer that logs every Apple80211 ioctl of a process (request and reply
  bytes). With `TAP_SHIM=1` it applies the same translations as the kext, in user space.
- `wl.c`: loads a chosen IO80211 library (Tahoe's stock one or Ventura's `IO80211Old.dylib`) and calls
  `Apple80211Scan` and, optionally, `Apple80211Associate` with a deliberately wrong passphrase.
- `raw.c`: raw scan request and scan result ioctls with chosen buffer sizes.
- `scan.swift`: a CoreWLAN scan.

Running `wl` against both libraries and comparing the taps gives the differences. With the shim on, the stock
Tahoe library returned the same networks as Ventura's and its association request became byte-identical to
Ventura's. Only then was the kext written. If another request turns out to need translation, the same tools will
find it. See [tools/README.md](tools/README.md).

## Build

```bash
git clone https://github.com/hamedafra/WiFiTahoeBridge
cd WiFiTahoeBridge
git clone --depth 1 https://github.com/acidanthera/MacKernelSDK
# put a DEBUG build of Lilu.kext (from the Lilu release zip) in this folder, then:
xcodebuild -project WiFiTahoeBridge.xcodeproj -configuration Release -arch x86_64 SYMROOT=build
```

The result is `build/Release/WiFiTahoeBridge.kext`. Built with Xcode 27.2, Lilu 1.7.3 headers.

A note for anyone starting a Lilu plugin from an existing one: if `OSBundleCompatibleVersion` in `Info.plist` is
greater than the kext's own version, OpenCore reports the injection as successful and the kernel then drops the
kext without a word. Version 0.1 of this project lost several reboots to that.

## Troubleshooting

- **Kext not in `kextstat`:** check the `Kernel > Add` entry and that Lilu is loaded and listed before it.
- **Card not visible at all:** the kernel half is missing or blocked. This kext cannot help with that.
- **Networks appear but joining fails:** please open an issue with the macOS build, the card, the network's
  security type, and a tap from `tools/` if you can make one.
- **Boot problem:** boot once with `-wtboff`.

## Credits

- [acidanthera](https://github.com/acidanthera) for Lilu and MacKernelSDK.
- [AirportBrcmFixup](https://github.com/acidanthera/AirportBrcmFixup) by lvs1974: this project started from its
  plugin skeleton.
- [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher) and its contributors, who brought
  these Wi-Fi cards back on modern macOS and maintain the kernel-side kexts this plugin relies on.

## License

BSD 3-Clause, see [LICENSE.txt](LICENSE.txt).
