#pragma once

namespace ever::browser {

class OverlayManager {
public:
    bool Initialize();
    void Tick();
    void Shutdown();

private:
    bool ready_ = false;
};

}