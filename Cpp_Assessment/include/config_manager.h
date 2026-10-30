#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct OverlayConfig {
    int width = 1280;
    int height = 720;
    bool topmost = true;
    bool displayExclusion = true;
};

struct ImageConfig {
    std::string format = "PNG"; // "PNG" or "JPEG"
    int quality = 85;          // JPEG quality (0-100)
};

struct HotkeyMapping {
    std::string actionName;
    unsigned int virtualKey = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

struct AppConfiguration {
    int schemaVersion = 1;
    OverlayConfig overlay;
    ImageConfig image;
    std::vector<HotkeyMapping> hotkeys;
};

class ConfigManager {
public:
    ConfigManager(const std::wstring& configDir = L".");
    ~ConfigManager();

    bool LoadAllConfigurations();
    bool SaveAllConfigurations();

    AppConfiguration& GetConfig() { return m_config; }
    
    // Recovery defaults
    void ResetToDefaults();

private:
    std::wstring m_configDir;
    AppConfiguration m_config;

    bool LoadJsonFile(const std::wstring& filePath, nlohmann::json& outJson);
    bool SaveJsonFile(const std::wstring& filePath, const nlohmann::json& jsonDoc);
};