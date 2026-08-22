#include <coreinit/debug.h>
#include <wups.h>

WUPS_PLUGIN_NAME("WiiU Bridge");
WUPS_PLUGIN_DESCRIPTION("DSU bridge v4.1");
WUPS_PLUGIN_VERSION("v4.1.0");
WUPS_PLUGIN_AUTHOR("OpenCode");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    if (error) *error = real_error;
    return result;
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);

INITIALIZE_PLUGIN() {
    OSReport("[Bridge] Loaded v4.1");
}

ON_APPLICATION_START() {
    OSReport("[Bridge] App started");
}
