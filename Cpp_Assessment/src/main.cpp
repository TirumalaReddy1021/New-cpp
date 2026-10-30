
#include <Windows.h>
#include <iostream>

#include "overlay.h"

int main() {
    std::cout << "=== Project ACEx Sidebar ===" << std::endl;

    OverlayWindow overlay;

    if (!overlay.Initialize(GetModuleHandleW(nullptr), 1280, 720)) {
        std::cerr << "[Main] Failed to initialize the overlay."
                  << std::endl;
        return 1;
    }

    std::cout << "[Main] Overlay initialized." << std::endl;
    std::cout << "[Main] Press Insert to toggle the sidebar."
              << std::endl;

    overlay.Run();

    overlay.Shutdown();

    std::cout << "[Main] Overlay closed." << std::endl;
    return 0;
}
