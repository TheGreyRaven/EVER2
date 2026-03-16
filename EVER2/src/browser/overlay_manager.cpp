#include "ever/browser/overlay_manager.h"
#include "ever/browser/native_overlay_renderer.h"

#include "ever/platform/debug_console.h"

namespace ever::browser {

bool OverlayManager::Initialize() {
    if (ready_) {
        return true;
    }

    const bool initialized = InitializeNativeOverlayRenderer();
    if (!initialized) {
        ever::platform::LogDebug(L"[EVER2] Overlay manager failed to initialize native in-process renderer.");
        ready_ = false;
        return false;
    }

    ever::platform::LogDebug(L"[EVER2] Overlay manager initialized in native present-hook mode.");
    ready_ = true;
    return true;
}

void OverlayManager::Tick() {
    if (!ready_) {
        return;
    }

    TickNativeOverlayRenderer();
}

void OverlayManager::Shutdown() {
    if (!ready_) {
        return;
    }

    ShutdownNativeOverlayRenderer();
    ready_ = false;
}

}