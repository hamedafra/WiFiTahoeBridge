#!/bin/sh
# Lets macOS 26 Tahoe advertise over Bluetooth LE on controllers without LE multiple-advertising support
# (for example the BCM20702 in the ASUS BT400). AirDrop needs this: other devices only open AWDL for a Mac they
# hear over Bluetooth. Uses Apple's own bluetoothd setting LE/SupportsMultipleAdvertising; nothing is patched.
#   sudo sh bt-single-advertising.sh on|off|status
set -e
PLIST=/Library/Preferences/com.apple.MobileBluetooth.debug
case "$1" in
  on)
    defaults write "$PLIST" LE -dict SupportsMultipleAdvertising -bool false
    defaults write com.apple.MobileBluetooth.debug LE -dict SupportsMultipleAdvertising -bool false
    killall bluetoothd
    echo "on: bluetoothd restarted (a Bluetooth mouse or keyboard reconnects in a few seconds)" ;;
  off)
    defaults delete "$PLIST" LE 2>/dev/null || true
    defaults delete com.apple.MobileBluetooth.debug LE 2>/dev/null || true
    killall bluetoothd
    echo "off: bluetoothd restarted" ;;
  status)
    defaults read "$PLIST" 2>/dev/null || echo "not set"
    /usr/bin/log show --last 10m --predicate 'process == "bluetoothd" AND (eventMessage CONTAINS "Overriding Multiple Advertising" OR eventMessage CONTAINS "stackDidStart() -- status" OR eventMessage CONTAINS "LE_InitMultipleAdvertising")' --style compact | tail -4 ;;
  *) echo "usage: sudo sh $0 on|off|status"; exit 1 ;;
esac
