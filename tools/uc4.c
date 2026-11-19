// Try to open the legacy family's async event user client on an interface. uc4 <ifname> <type>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    CFStringRef name = CFStringCreateWithCString(0, argv[1], kCFStringEncodingUTF8), key = CFSTR("IOInterfaceName"), top = CFSTR("IOPropertyMatch");
    CFDictionaryRef pm = CFDictionaryCreate(0, (const void **)&key, (const void **)&name, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryRef m = CFDictionaryCreate(0, (const void **)&top, (const void **)&pm, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    io_service_t s = IOServiceGetMatchingService(0, m); io_name_t c = ""; if (s) IOObjectGetClass(s, c);
    io_connect_t conn = 0; kern_return_t r = IOServiceOpen(s, mach_task_self(), atoi(argv[2]), &conn);
    printf("%s class %s open type %s -> 0x%x conn 0x%x\n", argv[1], c, argv[2], r, conn);
    if (!r) { io_name_t cc = ""; IOObjectGetClass(conn, cc); printf("  (connection object class lookup: %s)\n", cc); IOServiceClose(conn); }
    return 0;
}
