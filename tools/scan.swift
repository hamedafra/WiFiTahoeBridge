import CoreWLAN
import Foundation
let c = CWWiFiClient.shared()
guard let i = c.interface() else { print("no interface"); exit(1) }
print("iface:", i.interfaceName ?? "?", "power:", i.powerOn(), "mode:", i.interfaceMode().rawValue, "phy:", i.activePHYMode().rawValue, "country:", i.countryCode() ?? "nil")
do {
    let nets = try i.scanForNetworks(withSSID: nil)
    print("scan OK, networks:", nets.count)
    for n in nets.sorted(by: { $0.rssiValue > $1.rssiValue }).prefix(8) {
        print("  rssi", n.rssiValue, "ch", n.wlanChannel?.channelNumber ?? -1, "ssid", (n.ssid ?? "<hidden by privacy>").isEmpty ? "<empty>" : "<present>")
    }
} catch { print("scan FAILED:", error) }
