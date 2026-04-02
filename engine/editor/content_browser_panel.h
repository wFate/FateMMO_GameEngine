#pragma once
#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace fate {

class NetClient;

class ContentBrowserPanel {
public:
    bool isOpen() const { return open_; }
    void setOpen(bool v) { open_ = v; }
    void setNetClient(NetClient* nc) { netClient_ = nc; }
    void draw();

    // Called by game_app when server responses arrive
    void onContentListReceived(uint8_t contentType, const std::string& json);
    void onAdminResult(uint8_t requestType, bool success, const std::string& message);
    void onValidationReport(const std::vector<std::pair<uint8_t, std::string>>& issues);

private:
    bool open_ = false;
    NetClient* netClient_ = nullptr;

    // Toast
    std::string toastMessage_;
    float toastTimer_ = 0.0f;
    bool toastSuccess_ = false;

    // Per-tab state
    nlohmann::json mobList_ = nlohmann::json::array();
    int selectedMobIndex_ = -1;
    nlohmann::json editingMob_;
    bool mobListDirty_ = true;
    char mobFilterBuf_[128] = {};

    nlohmann::json itemList_ = nlohmann::json::array();
    int selectedItemIndex_ = -1;
    nlohmann::json editingItem_;
    bool itemListDirty_ = true;
    char itemFilterBuf_[128] = {};

    nlohmann::json lootList_ = nlohmann::json::array();
    std::string selectedLootTable_;
    bool lootListDirty_ = true;

    nlohmann::json spawnList_ = nlohmann::json::array();
    int selectedSpawnIndex_ = -1;
    nlohmann::json editingSpawn_;
    bool spawnListDirty_ = true;

    // Validation
    std::vector<std::pair<uint8_t, std::string>> validationIssues_;
    bool showErrors_ = true, showWarnings_ = true, showInfo_ = true;

    void drawMobsTab();
    void drawItemsTab();
    void drawLootTab();
    void drawSpawnsTab();
    void drawValidationTab();
    void drawToast();

    void requestContentList(uint8_t contentType);
    void saveContent(uint8_t contentType, bool isNew, const nlohmann::json& data);
    void deleteContent(uint8_t contentType, const std::string& id);

    // Field editor helpers (return true if modified)
    bool drawStringField(const char* label, nlohmann::json& obj, const char* key, bool readOnly = false);
    bool drawIntField(const char* label, nlohmann::json& obj, const char* key, int min = 0, int max = 999999);
    bool drawFloatField(const char* label, nlohmann::json& obj, const char* key, float min = 0.0f, float max = 999.0f);
    bool drawBoolField(const char* label, nlohmann::json& obj, const char* key);
    bool drawComboField(const char* label, nlohmann::json& obj, const char* key, const std::vector<std::string>& options);
};

} // namespace fate
