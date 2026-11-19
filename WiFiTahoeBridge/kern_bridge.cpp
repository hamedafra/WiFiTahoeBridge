//
//  kern_bridge.cpp
//  WiFiTahoeBridge
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <IOKit/IOLib.h>
#include <sys/errno.h>

#include "kern_bridge.hpp"
#include "kern_apiclient.hpp"

#include <libkern/copyio.h>
#include <string.h>

extern "C" int proc_selfpid(void);
extern "C" void proc_selfname(char *buf, int size);

static WTBridge *callbackBridge {nullptr};

static const char *pathLegacy[] {
	"/System/Library/Extensions/IO80211FamilyLegacy.kext/Contents/MacOS/IO80211FamilyLegacy"
};

static KernelPatcher::KextInfo kextList[] {
	{ "com.apple.iokit.IO80211FamilyLegacy", pathLegacy, arrsize(pathLegacy), {true}, {}, KernelPatcher::KextInfo::Unloaded },
};

extern int wtbAwdlEnabled;

WTBridge *WTBridge::shared() { return callbackBridge; }

const char *WTBridge::bsdName(void *virtualInterface) const {
	return symGetBSDName ? reinterpret_cast<const char *(*)(void *)>(symGetBSDName)(virtualInterface) : nullptr;
}

void WTBridge::init() {
	callbackBridge = this;
	IO80211APIUserClient::globalInit();
	SYSLOG("WTB", "WiFiTahoeBridge %s starting", xStringify(MODULE_VERSION));
	lilu.onKextLoadForce(kextList, arrsize(kextList),
		[](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
			static_cast<WTBridge *>(user)->processKext(patcher, index, address, size);
		}, this);
}

void WTBridge::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (kextList[0].loadIndex != index)
		return;

	KernelPatcher::RouteRequest requests[] {
		{ "__ZN16IO80211Interface14performCommandEP19IONetworkControllermPvS2_", wrapPerformCommand, orgPerformCommand },
	};
	if (patcher.routeMultiple(index, requests, address, size))
		SYSLOG("WTB", "routed IO80211Interface::performCommand");
	else {
		SYSLOG("WTB", "failed to route IO80211Interface::performCommand (%d)", patcher.getError());
		patcher.clearError();
	}

	// AWDL: Tahoe's wifip2pd needs the modern API user client on the AWDL interface, and the legacy family's
	// events for it. Kept apart from the Wi-Fi route above so a failure here leaves Wi-Fi alone.
	symGetBSDName = patcher.solveSymbol(index, "__ZN23IO80211VirtualInterface10getBSDNameEv", address, size);
	if (!symGetBSDName) {
		SYSLOG("WTB", "IO80211VirtualInterface::getBSDName not found (%d), AWDL client off", patcher.getError());
		patcher.clearError();
		return;
	}
	KernelPatcher::RouteRequest awdl[] {
		{ "__ZN23IO80211VirtualInterface13newUserClientEP4taskPvjP12OSDictionaryPP12IOUserClient", wrapNewUserClient, orgNewUserClient },
		{ "__ZN23IO80211VirtualInterface27IO80211InterfacePostMessageEjPvm", wrapVirtualPostMessage, orgVirtualPostMessage },
	};
	if (patcher.routeMultiple(index, awdl, address, size))
		SYSLOG("WTB", "routed the AWDL user client hooks");
	else {
		SYSLOG("WTB", "failed to route the AWDL hooks (%d)", patcher.getError());
		patcher.clearError();
		symGetBSDName = 0;
	}
}

int WTBridge::wrapPerformCommand(void *that, void *controller, unsigned long cmd, void *arg0, void *arg1) {
	auto org = FunctionCast(wrapPerformCommand, callbackBridge->orgPerformCommand);

	// SIOCSA80211 = _IOW('i', 200, apple80211req), SIOCGA80211 = _IOWR('i', 201, apple80211req)
	const unsigned long nr = cmd & 0xffff;
	const bool isSet = nr == 0x69c8, isGet = nr == 0x69c9;
	auto req = static_cast<Req *>(arg1);
	if ((!isSet && !isGet) || !req || !req->data || proc_selfpid() == 0)
		return org(that, controller, cmd, arg0, arg1);

	if (isGet && req->type == IocScanResult && req->len == ScanResultTahoe) {
		static const uint8_t zeros[ScanResultTahoe - ScanResultLegacy] {};
		int ret = 0;
		req->len = ScanResultLegacy;
		for (int i = 0; i < 75; i++) {            // up to 15 s while the scan is still running
			ret = org(that, controller, cmd, arg0, arg1);
			if (ret != EBUSY)
				break;
			IOSleep(200);
		}
		req->len = ScanResultTahoe;
		if (ret == 0)
			copyout(zeros, req->data + ScanResultLegacy, sizeof(zeros));
		return ret;
	}

	if (isSet && req->type == IocAssociate && req->len == AssocTahoe) {
		auto t = static_cast<uint8_t *>(IOMalloc(AssocTahoe + AssocLegacy));
		if (!t)
			return ENOMEM;
		uint8_t *v = t + AssocTahoe;
		int ret = copyin(req->data, t, AssocTahoe);
		if (ret == 0) {
			lilu_os_memcpy(v, t, 4);                    // version
			lilu_os_memcpy(v + 4, t + 4, 2);            // 32-bit field -> 16-bit
			lilu_os_memcpy(v + 6, t + 8, 2);            // 32-bit field -> 16-bit
			lilu_os_memcpy(v + 8, t + 12, AssocLegacy - 8);
			ret = copyout(v, req->data, AssocLegacy);   // legacy layout, in the caller's buffer
			if (ret == 0) {
				req->len = AssocLegacy;
				ret = org(that, controller, cmd, arg0, arg1);
				req->len = AssocTahoe;
				copyout(t, req->data, AssocTahoe);      // give the caller its own bytes back
			}
		}
		bzero(t, AssocTahoe + AssocLegacy);               // it held key material
		IOFree(t, AssocTahoe + AssocLegacy);
		return ret;
	}

	return org(that, controller, cmd, arg0, arg1);
}

IOReturn WTBridge::wrapNewUserClient(void *that, task_t task, void *securityID, uint32_t type, OSDictionary *props, IOUserClient **handler) {
	auto bridge = callbackBridge;
	if (type == 0 && wtbAwdlEnabled && handler && bridge->symGetBSDName) {
		// Only wifip2pd gets the new client. airportd also binds to awdl0; given a working user client there it
		// treats awdl0 as a second Wi-Fi interface and its auto-join stops with "driver not available", so it keeps
		// the old ioctl path, as in the configuration AirDrop was tested with. (newUserClient runs in the caller's
		// context, so proc_selfname is the process calling IOServiceOpen.)
		char caller[32] {};
		proc_selfname(caller, sizeof(caller));
		const char *name = bridge->bsdName(that);
		if (name && !strncmp(name, "awdl", 4) && !strcmp(caller, "wifip2pd")) {
			auto provider = static_cast<IOService *>(that);
			auto client = new IO80211APIUserClient;
			if (!client) return kIOReturnNoMemory;
			if (!client->initWithTask(task, securityID, type, props)) { client->release(); return kIOReturnNotPermitted; }
			if (!client->attach(provider)) { client->release(); return kIOReturnError; }
			if (!client->start(provider)) { client->detach(provider); client->release(); return kIOReturnError; }
			*handler = client;
			return kIOReturnSuccess;
		}
	}
	return FunctionCast(wrapNewUserClient, bridge->orgNewUserClient)(that, task, securityID, type, props, handler);
}

void WTBridge::wrapVirtualPostMessage(void *that, unsigned int msg, void *data, unsigned long len) {
	IO80211APIUserClient::postToClients(that, msg, data, len);
	FunctionCast(wrapVirtualPostMessage, callbackBridge->orgVirtualPostMessage)(that, msg, data, len);
}
