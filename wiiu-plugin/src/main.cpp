#include <coreinit/debug.h>
#include <wups.h>
#include "dsu_server.h"

WUPS_PLUGIN_NAME("WiiU Bridge Controller");
WUPS_PLUGIN_DESCRIPTION("DSU controller bridge");
WUPS_PLUGIN_VERSION("v4.4.0");
WUPS_PLUGIN_AUTHOR("OpenCode");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB();

bool networkStarted = false;

DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffer,
              uint32_t buffer_size, VPADReadError *error) {
    if (!networkStarted) {
        OSReport("[Bridge] Starting DSU...");
        dsu_server_init(26760);
        int r = dsu_server_start();
        OSReport("[Bridge] DSU start: %d", r);
        networkStarted = true;
    }
    VPADReadError real_error;
    int32_t result = real_VPADRead(chan, buffer, buffer_size, &real_error);
    if (error) *error = real_error;
    return result;
}

WUPS_MUST_REPLACE(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead);

INITIALIZE_PLUGIN() {
    OSReport("[Bridge] Plugin loaded");
}

ON_APPLICATION_START() {
    OSReport("[Bridge] Application started");
}

ON_APPLICATION_ENDS() {
    OSReport("[Bridge] Application ended");
    if (networkStarted) {
        dsu_server_stop();
        networkStarted = false;
    }
}
