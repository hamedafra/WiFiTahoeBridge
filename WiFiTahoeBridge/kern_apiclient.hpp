//
//  kern_apiclient.hpp
//  WiFiTahoeBridge
//
//  A stand-in for IO80211APIUserClient, the Wi-Fi "API user client" of the modern IO80211Family, on top of the
//  Ventura-era IO80211FamilyLegacy. Tahoe's IO80211.framework opens it with IOServiceOpen(type 0); wifip2pd needs it
//  on the AWDL interface for its event pipe. The contract was taken from the Tahoe kernel's own client and proven
//  first with a user-space model (tools/iouc.c) against the real library and the real wifip2pd.
//
//    selector 0  Apple80211 request pass-through (in: u32 cmd, u32 len, payload[0x3c00], apple80211req @0x3c08;
//                out: the raw payload; result: the handler's errno, untranslated)
//    selector 1  useIOUCWhenPossible -> 1        selector 5  useIOUCEventPipe -> 1      selector 7  isDKSupported -> 0
//    selector 3  event monitor init              selector 4  event monitor exit
//    selector 2  async: "event available"        memory type 1 = event ring, type 2 = ring state
//

#pragma once

#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLocks.h>
#include <net/kpi_interface.h>

class WTBAPIUserClient : public IOUserClient {
	OSDeclareDefaultStructors(WTBAPIUserClient)

public:
	static constexpr uint32_t RingSize    = 0x4b000;
	static constexpr uint32_t StateSize   = 0x1000;
	static constexpr uint32_t ScratchSize = 0x4000;
	static constexpr uint32_t MaxPayload  = 0x3c00;
	static constexpr uint32_t RequestSize = 0x3c30;
	static constexpr uint32_t RecordHeader = 0x30;
	static constexpr uint32_t MaxEvent    = 0x1000;

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties) override;
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;
	void free() override;
	IOReturn clientClose() override;
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory) override;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *dispatch,
							OSObject *target, void *reference) override;

	// Called from the legacy family's event path; must not block.
	void pushEvent(uint32_t msg, const void *data, size_t len);

	static void globalInit();
	static void postToClients(void *provider, uint32_t msg, const void *data, size_t len);

private:
	IOReturn runCommand(IOExternalMethodArguments *args);
	IOReturn monitorInit();

	task_t   fTask {nullptr};
	IOService *fProvider {nullptr};
	ifnet_t  fIfp {nullptr};
	char     fIfName[16] {};
	IOLock  *fLock {nullptr};

	IOBufferMemoryDescriptor *fScratch {nullptr};
	IOMemoryMap *fScratchMap {nullptr};
	mach_vm_address_t fScratchUser {0};

	IOBufferMemoryDescriptor *fRing {nullptr};
	IOBufferMemoryDescriptor *fState {nullptr};
	uint32_t fWrite {0};
	uint16_t fSeq {0};
	bool     fMonitoring {false};
	bool     fSubscribed {false};
	bool     fRegistered {false};
	OSAsyncReference64 fAsyncRef {};
	uint8_t  fEventMask[32] {};
};
