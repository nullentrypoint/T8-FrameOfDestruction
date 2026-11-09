#include "stdafx.h"

namespace globals {
    // Handle to our DLL module
    HMODULE mainModule = nullptr;
    // Main game window handle
    HWND mainWindow = nullptr;
    // Key to uninject and exit (F11 by default)
    int uninjectKey = VK_F11;
    // Key to open/close the ImGui menu (INSERT by default)
    int openMenuKey = VK_F1;
    // Flag controlling runtime debug logging
    bool enableDebugLog = false;
    // Currently active rendering backend
    Backend activeBackend = Backend::None; // DO NOT MODIFY THIS LINE.
    
    const std::string appName = "T8-FrameOfDestruction";
    const std::string appVersion = "v0.0.3-alpha";
}

namespace globals {
    void SetDebugLogging(bool enable) {
        enableDebugLog = enable;
    }
}

// Log initial global values for debugging
static void LogGlobals() {
    DebugLog("[Globals] mainModule=%p, mainWindow=%p, uninjectKey=0x%X, openMenuKey=0x%X, activeBackend=%d\n",
        globals::mainModule, globals::mainWindow, globals::uninjectKey, globals::openMenuKey,
        static_cast<int>(globals::activeBackend));
}

// Ensure we log when the DLL is loaded
struct GlobalsLogger {
    GlobalsLogger() {
        LogGlobals();
    }
};

static GlobalsLogger _globalsLogger;
