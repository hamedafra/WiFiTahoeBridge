//
//  kern_apiclient.cpp
//  WiFiTahoeBridge
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <string.h>

#include "kern_apiclient.hpp"
#include "kern_bridge.hpp"

OSDefineMetaClassAndStructors(WTBAPIUserClient, IOUserClient)

// debug.wtb_awdl: 1 lets the AWDL interface hand out this client. Off after every boot unless -wtbawdl is given,
// so a fault in this code cannot turn into a boot loop.
int wtbAwdlEnabled = 0;
static int statOpens = 0, statRequests = 0, statEvents = 0, statDrops = 0, statLastError = 0;
SYSCTL_INT(_debug, OID_AUTO, wtb_awdl, CTLFLAG_RW | CTLFLAG_LOCKED, &wtbAwdlEnabled, 0, "WiFiTahoeBridge: API user client on AWDL");
SYSCTL_INT(_debug, OID_AUTO, wtb_awdl_opens, CTLFLAG_RD | CTLFLAG_LOCKED, &statOpens, 0, "");
SYSCTL_INT(_debug, OID_AUTO, wtb_awdl_requests, CTLFLAG_RD | CTLFLAG_LOCKED, &statRequests, 0, "");
SYSCTL_INT(_debug, OID_AUTO, wtb_awdl_events, CTLFLAG_RD | CTLFLAG_LOCKED, &statEvents, 0, "");
SYSCTL_INT(_debug, OID_AUTO, wtb_awdl_drops, CTLFLAG_RD | CTLFLAG_LOCKED, &statDrops, 0, "");
SYSCTL_INT(_debug, OID_AUTO, wtb_awdl_lasterr, CTLFLAG_RD | CTLFLAG_LOCKED, &statLastError, 0, "");

static constexpr size_t MaxClients = 4;
static WTBAPIUserClient *clients[MaxClients] {};
static IOSimpleLock *registryLock {nullptr};   // clients[], and each client's ring writer and async reference

void WTBAPIUserClient::globalInit() {
	registryLock = IOSimpleLockAlloc();
	if (!registryLock) return;
	if (checkKernelArgument("-wtbawdl")) wtbAwdlEnabled = 1;
	sysctl_register_oid(&sysctl__debug_wtb_awdl);
	sysctl_register_oid(&sysctl__debug_wtb_awdl_opens);
	sysctl_register_oid(&sysctl__debug_wtb_awdl_requests);
	sysctl_register_oid(&sysctl__debug_wtb_awdl_events);
	sysctl_register_oid(&sysctl__debug_wtb_awdl_drops);
	sysctl_register_oid(&sysctl__debug_wtb_awdl_lasterr);
}

bool WTBAPIUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties) {
	if (!IOUserClient::initWithTask(owningTask, securityID, type, properties)) return false;
	// Apple's client wants the com.apple.wlan.authentication entitlement; the only caller we serve is wifip2pd (root).
	if (!owningTask || clientHasPrivilege(securityID, kIOClientPrivilegeAdministrator) != kIOReturnSuccess) return false;
	fTask = owningTask;
	return true;
}

bool WTBAPIUserClient::start(IOService *provider) {
	if (!registryLock || !IOUserClient::start(provider)) return false;
	fProvider = provider;
	setProperty("IOUserClientDefaultLocking", kOSBooleanTrue);
	setProperty("IOUserClientDefaultLockingSetProperties", kOSBooleanTrue);
	setProperty("IOUserClientDefaultLockingSingleThreadExternalMethod", kOSBooleanFalse);
	setProperty("IOUserClientEntitlements", kOSBooleanFalse);

	const char *name = WTBridge::shared() ? WTBridge::shared()->bsdName(provider) : nullptr;
	if (!name || !name[0]) return false;
	strlcpy(fIfName, name, sizeof(fIfName));
	if (ifnet_find_by_name(fIfName, &fIfp) != 0 || !fIfp) { fIfp = nullptr; return false; }

	fLock = IOLockAlloc();
	// Requests are handed to the legacy family the way a socket ioctl would: with a user address for the payload.
	// This buffer is that address: kernel memory also mapped into the client's task.
	fScratch = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared, ScratchSize, page_size);
	if (!fLock || !fScratch) return false;
	fScratchMap = fScratch->createMappingInTask(fTask, 0, kIOMapAnywhere, 0, 0);
	if (!fScratchMap) return false;
	fScratchUser = fScratchMap->getAddress();
	if (!fScratchUser) return false;

	bool placed = false;
	IOInterruptState st = IOSimpleLockLockDisableInterrupt(registryLock);
	for (size_t i = 0; i < MaxClients && !placed; i++)
		if (!clients[i]) { clients[i] = this; placed = true; }
	IOSimpleLockUnlockEnableInterrupt(registryLock, st);
	if (!placed) return false;
	fRegistered = true;
	statOpens++;
	return true;
}

void WTBAPIUserClient::stop(IOService *provider) {
	OSAsyncReference64 ref; bool hadRef = false;
	if (registryLock) {
		IOInterruptState st = IOSimpleLockLockDisableInterrupt(registryLock);
		if (fRegistered) for (size_t i = 0; i < MaxClients; i++) if (clients[i] == this) clients[i] = nullptr;
		fRegistered = false; fMonitoring = false;
		if (fSubscribed) { bcopy(fAsyncRef, ref, sizeof(ref)); hadRef = true; fSubscribed = false; }
		IOSimpleLockUnlockEnableInterrupt(registryLock, st);
	}
	if (hadRef) releaseAsyncReference64(ref);
	IOUserClient::stop(provider);
}

void WTBAPIUserClient::free() {
	OSSafeReleaseNULL(fScratchMap);
	OSSafeReleaseNULL(fScratch);
	OSSafeReleaseNULL(fRing);
	OSSafeReleaseNULL(fState);
	if (fIfp) { ifnet_release(fIfp); fIfp = nullptr; }
	if (fLock) { IOLockFree(fLock); fLock = nullptr; }
	IOUserClient::free();
}

IOReturn WTBAPIUserClient::clientClose() {
	terminate();
	return kIOReturnSuccess;
}

IOReturn WTBAPIUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory) {
	IOBufferMemoryDescriptor *m = type == 1 ? fRing : type == 2 ? fState : nullptr;
	if (!m || !memory) return kIOReturnError;
	m->retain();
	*memory = m;
	if (options) *options = 0;
	return kIOReturnSuccess;
}

IOReturn WTBAPIUserClient::monitorInit() {
	IOLockLock(fLock);
	if (!fRing) {
		auto ring = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared, RingSize, page_size);
		auto state = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared, StateSize, page_size);
		if (!ring || !state) {
			OSSafeReleaseNULL(ring); OSSafeReleaseNULL(state);
			IOLockUnlock(fLock);
			return kIOReturnNoMemory;
		}
		bzero(ring->getBytesNoCopy(), RingSize);
		bzero(state->getBytesNoCopy(), StateSize);
		fWrite = 0;
		fState = state;
		fRing = ring;
	}
	fMonitoring = true;
	IOLockUnlock(fLock);
	return kIOReturnSuccess;
}

IOReturn WTBAPIUserClient::runCommand(IOExternalMethodArguments *args) {
	IOMemoryDescriptor *inMD = args->structureInputDescriptor;
	uint64_t inSize = inMD ? inMD->getLength() : args->structureInputSize;
	if (inSize < RequestSize || (!inMD && !args->structureInput) || !fIfp || !fScratchUser) return kIOReturnBadArgument;
	if (inMD && inMD->prepare(kIODirectionOut) != kIOReturnSuccess) return kIOReturnVMError;

	auto readIn = [&](uint64_t off, void *dst, uint32_t n) -> bool {
		if (inMD) return inMD->readBytes(off, dst, n) == n;
		lilu_os_memcpy(dst, static_cast<const uint8_t *>(args->structureInput) + off, n);
		return true;
	};

	IOReturn ret = kIOReturnBadArgument;
	IOLockLock(fLock);
	do {
		uint32_t hdr[2] {};
		WTBridge::Req req {};
		if (!readIn(0, hdr, sizeof(hdr)) || !readIn(0x3c08, &req, sizeof(req))) break;
		const bool set = (hdr[0] | 0x80000) == 0x802869c8;
		const uint32_t dlen = hdr[1];
		if (dlen > MaxPayload) { ret = EINVAL; break; }

		auto scratch = static_cast<uint8_t *>(fScratch->getBytesNoCopy());
		WTBridge::Req k {};
		strlcpy(k.ifname, fIfName, sizeof(k.ifname));
		k.type = req.type;
		k.val = req.val;
		statRequests++;

		if (set) {
			if (args->structureOutput) args->structureOutputSize = 0;
			if (args->structureOutputDescriptor) args->structureOutputDescriptorSize = 0;
			if (req.type == 0x234) {
				// Event filter: the real client keeps it too. The library filters again on its side, so every
				// event is delivered and the mask is only remembered.
				bzero(fEventMask, sizeof(fEventMask));
				if (dlen && !readIn(8, fEventMask, dlen < sizeof(fEventMask) ? dlen : sizeof(fEventMask))) break;
				ret = 0;
				break;
			}
			if (dlen && !readIn(8, scratch, dlen)) break;
			k.len = dlen;
			k.data = dlen ? fScratchUser : 0;
			ret = ifnet_ioctl(fIfp, 0, 0x802869c8UL, &k);
		} else {
			IOMemoryDescriptor *outMD = args->structureOutputDescriptor;
			uint64_t want = outMD ? outMD->getLength() : (args->structureOutput ? args->structureOutputSize : 0);
			const uint32_t cap = want > MaxPayload ? MaxPayload : static_cast<uint32_t>(want);
			bzero(scratch, cap);
			k.len = cap;
			k.data = cap ? fScratchUser : 0;
			ret = ifnet_ioctl(fIfp, 0, 0xc02869c9UL, &k);
			if (ret == 0) {
				const uint32_t n = k.len <= cap ? k.len : cap;
				if (outMD) {
					if (outMD->prepare(kIODirectionIn) == kIOReturnSuccess) {
						if (n) outMD->writeBytes(0, scratch, n);
						outMD->complete(kIODirectionIn);
						args->structureOutputDescriptorSize = n;
					} else ret = kIOReturnVMError;
				} else if (args->structureOutput) {
					if (n) lilu_os_memcpy(args->structureOutput, scratch, n);
					args->structureOutputSize = n;
				}
			}
		}
		if (ret) statLastError = (req.type << 8) | (ret & 0xff);
	} while (0);
	IOLockUnlock(fLock);
	if (inMD) inMD->complete(kIODirectionOut);
	return ret;   // an errno, as the real client returns it
}

IOReturn WTBAPIUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *dispatch,
										  OSObject *target, void *reference) {
	if (!args || isInactive() || !fLock) return kIOReturnNotAttached;
	switch (selector) {
		case 0:
			return runCommand(args);
		case 1: case 5: case 7:
			if (!args->structureOutput || args->structureOutputSize < 1) return kIOReturnBadArgument;
			*static_cast<uint8_t *>(args->structureOutput) = selector == 7 ? 0 : 1;
			args->structureOutputSize = 1;
			return kIOReturnSuccess;
		case 2: {
			if (!args->asyncWakePort || !args->asyncReference || args->asyncReferenceCount == 0) return kIOReturnBadArgument;
			OSAsyncReference64 old; bool hadOld = false;
			IOInterruptState st = IOSimpleLockLockDisableInterrupt(registryLock);
			if (fSubscribed) { bcopy(fAsyncRef, old, sizeof(old)); hadOld = true; }
			bzero(fAsyncRef, sizeof(fAsyncRef));
			uint32_t n = args->asyncReferenceCount < kOSAsyncRef64Count ? args->asyncReferenceCount : kOSAsyncRef64Count;
			bcopy(args->asyncReference, fAsyncRef, n * sizeof(fAsyncRef[0]));
			fSubscribed = true;
			IOSimpleLockUnlockEnableInterrupt(registryLock, st);
			if (hadOld) releaseAsyncReference64(old);
			return kIOReturnSuccess;
		}
		case 3:
			return monitorInit();
		case 4: {
			OSAsyncReference64 old; bool hadOld = false;
			IOInterruptState st = IOSimpleLockLockDisableInterrupt(registryLock);
			fMonitoring = false;
			if (fSubscribed) { bcopy(fAsyncRef, old, sizeof(old)); hadOld = true; fSubscribed = false; }
			IOSimpleLockUnlockEnableInterrupt(registryLock, st);
			if (hadOld) releaseAsyncReference64(old);
			return kIOReturnSuccess;
		}
		default:
			return kIOReturnUnsupported;
	}
}

void WTBAPIUserClient::pushEvent(uint32_t msg, const void *data, size_t len) {
	if (len > MaxEvent || (len && !data)) { statDrops++; return; }
	clock_sec_t secs = 0; clock_usec_t usecs = 0;
	clock_get_calendar_microtime(&secs, &usecs);
	const uint32_t n = static_cast<uint32_t>(len), pad = (4 - (n & 3)) & 3, need = RecordHeader + n + pad;

	OSAsyncReference64 ref; bool notify = false;
	IOInterruptState st = IOSimpleLockLockDisableInterrupt(registryLock);
	if (fMonitoring && fRing && fState) {
		auto ring = static_cast<uint8_t *>(fRing->getBytesNoCopy());
		auto state = static_cast<volatile uint32_t *>(fState->getBytesNoCopy());
		uint32_t w = fWrite, r = state[0];          // r is written by user space: never trusted as an offset
		if (r >= RingSize || (r & 3)) r = w;
		const uint32_t fill = (RingSize - w < RecordHeader) ? RingSize - w : 0;
		const uint32_t avail = (r == w) ? RingSize - 4 : (r + RingSize - w - 4) % RingSize;
		if (need + fill <= avail) {
			for (uint32_t i = 0; i < fill; i += 4) *reinterpret_cast<uint32_t *>(ring + w + i) = 0x50;
			if (fill) w = 0;
			uint8_t h[RecordHeader] {};
			*reinterpret_cast<uint16_t *>(h + 6) = ++fSeq;
			*reinterpret_cast<uint64_t *>(h + 8) = secs;
			*reinterpret_cast<uint64_t *>(h + 16) = usecs;
			*reinterpret_cast<uint16_t *>(h + 0x18) = static_cast<uint16_t>(n);
			*reinterpret_cast<uint16_t *>(h + 0x1a) = static_cast<uint16_t>(pad);
			*reinterpret_cast<uint32_t *>(h + 0x1c) = msg;
			strlcpy(reinterpret_cast<char *>(h + 0x20), fIfName, 16);
			lilu_os_memcpy(ring + w + 4, h + 4, RecordHeader - 4);
			const uint32_t p = (w + RecordHeader) % RingSize, first = (RingSize - p < n) ? RingSize - p : n;
			if (first) lilu_os_memcpy(ring + p, data, first);
			if (n > first) lilu_os_memcpy(ring, static_cast<const uint8_t *>(data) + first, n - first);
			*reinterpret_cast<volatile uint32_t *>(ring + w) = 0x57;   // record complete
			w = (w + need) % RingSize;
			fWrite = w;
			state[1] = w;
			statEvents++;
		} else {
			statDrops++;
		}
		if (fSubscribed) { bcopy(fAsyncRef, ref, sizeof(ref)); notify = true; }
	}
	IOSimpleLockUnlockEnableInterrupt(registryLock, st);
	if (notify) sendAsyncResult64WithOptions(ref, kIOReturnSuccess, nullptr, 0, kIOUserNotifyOptionCanDrop);
}

void WTBAPIUserClient::postToClients(void *provider, uint32_t msg, const void *data, size_t len) {
	if (!registryLock) return;
	WTBAPIUserClient *list[MaxClients]; size_t count = 0;
	IOInterruptState st = IOSimpleLockLockDisableInterrupt(registryLock);
	for (size_t i = 0; i < MaxClients; i++)
		if (clients[i] && clients[i]->fProvider == provider && clients[i]->fMonitoring) { clients[i]->retain(); list[count++] = clients[i]; }
	IOSimpleLockUnlockEnableInterrupt(registryLock, st);
	for (size_t i = 0; i < count; i++) { list[i]->pushEvent(msg, data, len); list[i]->release(); }
}
