//
//  kern_bridge.hpp
//  WiFiTahoeBridge
//
//  Lets macOS 26 (Tahoe) user space drive the Ventura-era IO80211FamilyLegacy
//  that OpenCore injects for Broadcom BCM4360-class cards, with no root patch.
//
//  Tahoe's IO80211.framework sends three things the legacy family does not take:
//    - APPLE80211_IOC_SCAN_RESULT with a 0x8e0-byte buffer (legacy: exactly 0x48c;
//      same header, the tail is a longer IE area and one new field)
//    - it reads scan results right away instead of waiting for the scan-done
//      event, so the legacy family answers EBUSY
//    - APPLE80211_IOC_ASSOCIATE with a 908-byte struct (legacy: 592; two 16-bit
//      fields became 32-bit, and a new tail was added)
//  All three were measured and reproduced in user space first (see the README).
//

#pragma once

#include <Headers/kern_patcher.hpp>

class WTBridge {
public:
	void init();

	// apple80211req as the ioctl carries it
	struct Req {
		char     ifname[16];
		uint32_t type;
		int32_t  val;
		uint32_t len;
		uint32_t pad;
		uint64_t data;   // user address
	};

	static constexpr uint32_t IocScanResult = 11;
	static constexpr uint32_t IocAssociate  = 20;

	static constexpr uint32_t ScanResultLegacy = 0x48c;
	static constexpr uint32_t ScanResultTahoe  = 0x8e0;
	static constexpr uint32_t AssocLegacy      = 592;
	static constexpr uint32_t AssocTahoe       = 908;

private:
	void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);

	static int wrapPerformCommand(void *that, void *controller, unsigned long cmd, void *arg0, void *arg1);

	mach_vm_address_t orgPerformCommand {0};
};
