# WiFiTahoeBridge

**Broadcom Wi-Fi (BCM4360 class) and AirDrop on macOS 26 Tahoe, without OpenCore Legacy Patcher root patches.**

[فارسی](README.fa.md)

A small [Lilu](https://github.com/acidanthera/Lilu) plugin for hackintoshes (OpenCore). It lets the stock Wi-Fi and
AirDrop software of macOS Tahoe talk to the old Broadcom driver stack that OpenCore injects, so the system volume
stays sealed and untouched: no `IO80211.framework` / `wifip2pd` replacement, no root patching after every update.

| | |
|---|---|
| Status | Works on the author's machine: Wi-Fi (scan, join, DHCP) and AirDrop in both directions with an iPhone and a Mac |
| Tested on | macOS 26.7.3 (25G342), one PC, BCM4360 Wi-Fi + BCM20702 Bluetooth (ASUS BT400) |
| Version | 0.3.0 |

## The problem it solves

Apple removed the drivers for older Broadcom cards (BCM4360, BCM43602, BCM4350, and BCM4352 as used with
AirportBrcmFixup) in macOS 14 Sonoma. The usual fix today has two halves:

1. **Kernel half**, injected by OpenCore: `IOSkywalkFamily` (downgraded), `IO80211FamilyLegacy` and
   `AirPortBrcmNIC`. No change to the system volume.
2. **User-space half**, the *root patch* done by OpenCore Legacy Patcher ("Modern Wireless"): replace
   `IO80211.framework`, `WiFiPeerToPeer.framework` and `wifip2pd` on the system volume with Ventura's copies.

The second half is the painful one. It breaks the sealed system volume, needs SIP lowered and AMFI worked around,
and has to be applied again after every macOS update.

Without the root patch on Tahoe, the kernel half still loads and the card shows up, but:
- a scan returns nothing and you cannot join a network, because Tahoe's `IO80211.framework` and the Ventura-era
  kernel family disagree on three requests;
- AirDrop never starts, because Tahoe's `wifip2pd` needs a kernel interface the old family does not have (the
  modern Wi-Fi "API user client" with its event pipe) and fails with `Unable to create AWDLInterface for awdl0
  because initialization error: Invalid Parameters`.

WiFiTahoeBridge fills both gaps inside the kernel. For AirDrop there is also a Bluetooth part, which needs no code:
see [Bluetooth](#bluetooth-needed-for-airdrop).

## What works and what does not

| Feature | State |
|---|---|
| Scan, network list in the Wi-Fi menu | Works |
| Join a WPA2/WPA3 Personal network, DHCP, traffic | Works (5 GHz, 80 MHz, 802.11ac, 1053 Mbps link rate) |
| Auto-join after boot | Works |
| AWDL: `awdl0` / `llw0` up, peer discovery, data path | Works (with an iPhone and a Mac on macOS 27) |
| AirDrop | Works both ways (iPhone to hackintosh, hackintosh to iPhone / Mac), with the Bluetooth setting below |
| Sealed system volume, no root patch | Yes |
| Handoff, Universal Clipboard, AirPlay to Mac, Sidecar | Not tested yet |
| Sleep/wake, long uptime | Not tested yet |
| WPA3-only, WPA Enterprise, open/WEP networks, hotspot | Not tested |
| Cards other than BCM4360, Bluetooth controllers other than BCM20702, real Macs | Not tested |
| macOS 15 Sequoia and older | Not supported (the kext only starts on Darwin 25) |

This is tested on a single machine. Keep a bootable backup of your EFI (a USB stick) before you try it.

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
- For AirDrop: working Bluetooth and the same Apple Account on both devices, as on a real Mac.

If the card already appears in System Information > Wi-Fi on Tahoe but finds no networks, the kernel half is in
place and this kext is the missing piece.

## Install

1. Download `WiFiTahoeBridge-0.3.0-RELEASE.zip` from
   [Releases](https://github.com/hamedafra/WiFiTahoeBridge/releases) and copy `WiFiTahoeBridge.kext` to
   `EFI/OC/Kexts`.
2. Add it to `Kernel > Add` in `config.plist`, anywhere after `Lilu.kext` and after the three kexts above:

   ```xml
   <dict>
       <key>Arch</key>
       <string>x86_64</string>
       <key>BundlePath</key>
       <string>WiFiTahoeBridge.kext</string>
       <key>Comment</key>
       <string>Tahoe Wi-Fi and AWDL on IO80211FamilyLegacy</string>
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
5. For AirDrop, do the [Bluetooth](#bluetooth-needed-for-airdrop) step once.

To check that it loaded and that `wifip2pd` uses it:

```bash
kextstat | grep WiFiTahoeBridge
```

```bash
sysctl debug.wtb_awdl debug.wtb_awdl_opens debug.wtb_awdl_events debug.wtb_awdl_drops
```

`debug.wtb_awdl_opens` should be 1 shortly after login, and `debug.wtb_awdl_events` grows while AirDrop is open.

### Boot arguments

| Argument | Effect |
|---|---|
| `-wtboff` | Disable the whole kext |
| `-wtbawdloff` | Keep Wi-Fi, disable the AWDL / AirDrop part |
| `-wtbdbg` | Debug logging (needs a DEBUG build) |
| `-wtbbeta` | Load on a Darwin version newer than 25 |

The AWDL part is also off in safe mode and in the installer/recovery. At run time it can be switched with
`sudo sysctl debug.wtb_awdl=0` (or `=1`) followed by `sudo launchctl kickstart -k system/com.apple.wifip2pd`.

## Bluetooth (needed for AirDrop)

AirDrop finds devices over Bluetooth LE, then moves the data over AWDL. A device only opens AWDL for a Mac it has
heard advertising over Bluetooth. On Tahoe with a BCM20702 controller (ASUS BT400 here) the Mac never advertises:
`bluetoothd` asks the controller for LE multiple advertising at start-up, the controller rejects it
(`LE_InitMultipleAdvertising failed with status 152`), and from then on every advertising request ends in
`Stack is not started, updateAdvertisement returning`. The Mac then sees other devices in the AirDrop window, but
it is invisible to them and every send fails after a few seconds.

`bluetoothd` has its own setting to turn that feature off, and it honours it on the public build:

```bash
sudo sh tools/bt-single-advertising.sh on
```

which runs:

```bash
sudo defaults write /Library/Preferences/com.apple.MobileBluetooth.debug LE -dict SupportsMultipleAdvertising -bool false
```

```bash
sudo defaults write com.apple.MobileBluetooth.debug LE -dict SupportsMultipleAdvertising -bool false
```

```bash
sudo killall bluetoothd
```

The log then shows `Warning: Overriding Multiple Advertising support - disabled` and
`stackDidStart() -- status = OI_OK`, and advertising starts. The setting is a preference file: it survives reboots
and is not touched by the kext. `sudo sh tools/bt-single-advertising.sh off` removes it. (Both domains were written
in the test; which one `bluetoothd` reads was not separated.)

This part has nothing to do with Wi-Fi and may help any hackintosh with a Bluetooth 4.0 Broadcom controller on Tahoe.
Controllers with Bluetooth 5 extended advertising probably do not need it.

## How it works

### Wi-Fi requests

Tahoe's `IO80211.framework` reaches the driver with the same Apple80211 ioctls as Ventura's
(`SIOCSA80211` / `SIOCGA80211` on a socket, with an `apple80211req`). Three requests differ:

| Request | Tahoe sends | Legacy family wants | Translation |
|---|---|---|---|
| `APPLE80211_IOC_SCAN_RESULT` (11, get) | 0x8e0-byte buffer | exactly 0x48c bytes | call with 0x48c, zero the rest |
| same request, timing | reads results right away | `EBUSY` until the scan is done | retry every 200 ms, up to 15 s |
| `APPLE80211_IOC_ASSOCIATE` (20, set) | 908-byte struct | 592-byte struct | two 16-bit fields became 32-bit and a tail was added |

The kext routes `IO80211Interface::performCommand` in `IO80211FamilyLegacy` and rewrites only these requests, only
when they come from a user process with exactly the Tahoe sizes.

### AWDL

Tahoe's `wifip2pd` opens an IOKit user client of type 0 on the AWDL interface (`IOServiceOpen`), sends its
Apple80211 requests through it, and receives events through a shared-memory ring. On a real Mac that client is
`IO80211APIUserClient` of the modern `IO80211Family`. The legacy family has no such client, and it delivers AWDL
events only to an older, entitlement-gated client that Tahoe's software never opens.

The kext hooks `IO80211VirtualInterface::newUserClient` and, for type 0 on `awdl*` opened by `wifip2pd`, creates
its own `IO80211APIUserClient` (the class name is Apple's on purpose: `wifip2pd`'s sandbox only lets it open the
classes listed in its entitlement). `airportd` also binds to `awdl0`, but it keeps the old ioctl path: given the new
client there, it treats `awdl0` as a second Wi-Fi interface and its auto-join stops with "driver not available".
That client:
- passes requests (selector 0) to the legacy family through `ifnet_ioctl()`, unchanged; every AWDL request Tahoe's
  `wifip2pd` sent in testing was accepted as is, except one new request (575, OS eligibility) that is not supported;
- answers the capability queries (selectors 1, 5, 7) and keeps the event filter (request 0x234);
- keeps an event ring in the modern format (0x30-byte record header, marker 0x57, filler 0x50, read/write offsets in
  a second shared page) and signals "data available" with an async completion;
- fills that ring from a hook on `IO80211VirtualInterface::IO80211InterfacePostMessage`, which every AWDL event of
  the legacy family passes through.

Details, including the request numbers seen and the ring format, are in [docs/awdl.md](docs/awdl.md); the Wi-Fi
request layouts are in [docs/protocol.md](docs/protocol.md).

## How it was found: testing in user space first

Most of the work was measured and proven in user space on the running Tahoe system before any kernel code was
written. The tools are in [tools/](tools/): an ioctl logger (`tap.c`), a user-space model of the whole AWDL user
client (`iouc.c`) that made the real Tahoe library and the real `wifip2pd` create their AWDL interface without a
kext, a kernel-event listener, and small request helpers. See [tools/README.md](tools/README.md).

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
kext without a word.

## Troubleshooting

- **Kext not in `kextstat`:** check the `Kernel > Add` entry and that Lilu is loaded and listed before it.
- **Card not visible at all:** the kernel half is missing or blocked. This kext cannot help with that.
- **Networks appear but joining fails:** please open an issue with the macOS build, the card, the network's
  security type, and a tap from `tools/` if you can make one.
- **AirDrop: other devices show up but sending fails, and the Mac is not visible on the iPhone:** Bluetooth
  advertising; see [Bluetooth](#bluetooth-needed-for-airdrop).
- **AirDrop: nothing shows up:** check `debug.wtb_awdl_opens` (should be 1) and
  `log show --last 5m --predicate 'process == "wifip2pd"' | grep AWDLInterface`.
- **Boot problem:** boot once with `-wtbawdloff`, then with `-wtboff`.

## Credits

- [acidanthera](https://github.com/acidanthera) for Lilu and MacKernelSDK.
- [AirportBrcmFixup](https://github.com/acidanthera/AirportBrcmFixup) by lvs1974: this project started from its
  plugin skeleton.
- [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher) and its contributors, who brought
  these Wi-Fi cards back on modern macOS and maintain the kernel-side kexts this plugin relies on.
- [ForbiddenItlwm](https://github.com/retropineaple/ForbiddenItlwm) and
  [ax210-airdrop-awdl-hackintosh](https://github.com/kevineduardo/ax210-airdrop-awdl-hackintosh), whose notes on
  Apple80211 AWDL request numbers and on Bluetooth advertising for AirDrop were a useful reference (no code taken).

## License

BSD 3-Clause, see [LICENSE.txt](LICENSE.txt).
