#pragma once
#include <string>
#include <vector>

namespace fate {

class PaperDollPanel {
public:
    bool isOpen() const { return open_; }
    void setOpen(bool v) { open_ = v; }
    void draw();

private:
    bool open_ = false;

    // State
    int selectedTab_ = 0;  // 0=Bodies, 1=Hair, 2=Armor, 3=Hat, 4=Weapon, 5=Animations
    int previewGender_ = 0; // 0=Male, 1=Female
    int previewDirection_ = 0; // 0=Front, 1=Back, 2=Side
    std::string selectedEntry_;  // currently selected hairstyle or equipment style

    // Layer visibility for composite preview
    bool showBody_ = true;
    bool showHair_ = true;
    bool showArmor_ = false;
    bool showHat_ = false;
    bool showWeapon_ = false;

    // Preview selections
    std::string previewHairName_;
    std::string previewArmorStyle_;
    std::string previewHatStyle_;
    std::string previewWeaponStyle_;

    // Tab drawing methods
    void drawBodiesTab();
    void drawHairTab();
    void drawEquipmentTab(const std::string& category);
    void drawAnimationsTab();
    void drawCompositePreview();

    // Helper: file browse button that updates a path
    bool browseButton(const char* id, std::string& path);
};

} // namespace fate
