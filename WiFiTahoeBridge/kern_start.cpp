//
//  kern_start.cpp
//  WiFiTahoeBridge
//
//  Lilu plugin entry point.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>

#include "kern_bridge.hpp"

static WTBridge bridge;

static const char *bootargOff[]   { "-wtboff" };
static const char *bootargDebug[] { "-wtbdbg" };
static const char *bootargBeta[]  { "-wtbbeta" };

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	bootargBeta,
	arrsize(bootargBeta),
	KernelVersion::Tahoe,
	KernelVersion::Tahoe,
	[]() {
		bridge.init();
	}
};
