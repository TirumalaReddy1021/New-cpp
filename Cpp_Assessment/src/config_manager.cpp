#include "config_manager.h"
#include <fstream>
#include <iostream>

ConfigManager::ConfigManager(const std::wstring& configDir)
    : m_configDir(configDir) {
    ResetToDefaults();
}

ConfigManager::~ConfigManager() {}

void ConfigManager::ResetToDefaults() {
    m_config.schemaVersion = 1;
    m_config.overlay = { 1280, 720, true, true };
    m_config.image = { "PNG", 85 };
    m_config.hotkeys = {
        { "CaptureScreen", 0x43, true, true, false }, // Ctrl + Alt + C
        { "ToggleHUD", 0x48, true, true, false }       // Ctrl + Alt + H
    };
}

bool ConfigManager::LoadJsonFile(const std::wstring& filePath, nlohmann::json& outJson) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;
    try {
        file >> outJson;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[ConfigManager] JSON parse error in " << std::string(filePath.begin(), filePath.end()) 
                  << ": " << e.what() << std::endl;
        return false;
    }
    return true;
}

bool ConfigManager::SaveJsonFile(const std::wstring& filePath, const nlohmann::json& jsonDoc) {
    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    file << jsonDoc.dump(4);
    return true;
}

bool ConfigManager::LoadAllConfigurations() {
    std::wstring configPath = m_configDir + L"/config.json";
    std::wstring hotkeysPath = m_configDir + L"/hotkeys.json";
    std::wstring metadataPath = m_configDir + L"/metadata.json";

    nlohmann::json jConfig, jHotkeys, jMeta;

    // Load main config.json
    if (LoadJsonFile(configPath, jConfig)) {
        try {
            if (jConfig.contains("schemaVersion")) m_config.schemaVersion = jConfig["schemaVersion"].get<int>();
            if (jConfig.contains("overlay")) {
                m_config.overlay.width = jConfig["overlay"].value("width", 1280);
                m_config.overlay.height = jConfig["overlay"].value("height", 720);
                m_config.overlay.topmost = jConfig["overlay"].value("topmost", true);
                m_config.overlay.displayExclusion = jConfig["overlay"].value("displayExclusion", true);
            }
            if (jConfig.contains("image")) {
                m_config.image.format = jConfig["image"].value("format", "PNG");
                m_config.image.quality = jConfig["image"].value("quality", 85);
            }
        } catch (...) {
            std::cerr << "[ConfigManager] Malformed structure in config.json. Restoring defaults." << std::endl;
            ResetToDefaults();
            return false;
        }
    } else {
        std::wcout << L"[ConfigManager] config.json not found. Generating default configuration." << std::endl;
        SaveAllConfigurations();
    }

    // Load hotkeys.json
    if (LoadJsonFile(hotkeysPath, jHotkeys)) {
        try {
            if (jHotkeys.contains("hotkeys") && jHotkeys["hotkeys"].is_array()) {
                m_config.hotkeys.clear();
                for (const auto& item : jHotkeys["hotkeys"]) {
                    HotkeyMapping hk;
                    hk.actionName = item.value("action", "");
                    hk.virtualKey = item.value("vk", 0);
                    hk.ctrl = item.value("ctrl", false);
                    hk.alt = item.value("alt", false);
                    hk.shift = item.value("shift", false);
                    m_config.hotkeys.push_back(hk);
                }
            }
        } catch (...) {
            std::cerr << "[ConfigManager] Malformed structure in hotkeys.json." << std::endl;
        }
    }

    return true;
}

bool ConfigManager::SaveAllConfigurations() {
    std::wstring configPath = m_configDir + L"/config.json";
    std::wstring hotkeysPath = m_configDir + L"/hotkeys.json";
    std::wstring metadataPath = m_configDir + L"/metadata.json";

    // Serialize config.json
    nlohmann::json jConfig;
    jConfig["schemaVersion"] = m_config.schemaVersion;
    jConfig["overlay"] = {
        {"width", m_config.overlay.width},
        {"height", m_config.overlay.height},
        {"topmost", m_config.overlay.topmost},
        {"displayExclusion", m_config.overlay.displayExclusion}
    };
    jConfig["image"] = {
        {"format", m_config.image.format},
        {"quality", m_config.image.quality}
    };
    SaveJsonFile(configPath, jConfig);

    // Serialize hotkeys.json
    nlohmann::json jHotkeys;
    nlohmann::json jHotkeysArray = nlohmann::json::array();
    for (const auto& hk : m_config.hotkeys) {
        jHotkeysArray.push_back({
            {"action", hk.actionName},
            {"vk", hk.virtualKey},
            {"ctrl", hk.ctrl},
            {"alt", hk.alt},
            {"shift", hk.shift}
        });
    }
    jHotkeys["hotkeys"] = jHotkeysArray;
    SaveJsonFile(hotkeysPath, jHotkeys);

    // Serialize metadata.json
    nlohmann::json jMeta;
    jMeta["project"] = "Project ACEx";
    jMeta["architecture"] = "x64";
    jMeta["author"] = "Senior Windows Systems Architect";
    SaveJsonFile(metadataPath, jMeta);

    return true;
}