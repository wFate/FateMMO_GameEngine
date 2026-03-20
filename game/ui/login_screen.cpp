#include "game/ui/login_screen.h"
#include "imgui.h"

namespace fate {

void LoginScreen::draw() {
    // Center the login window
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImVec2 windowSize(400, 0); // auto height
    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

    const char* title = (state == LoginScreenState::Login) ? "Login###AuthWindow" : "Register###AuthWindow";
    ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // Server host/port
    ImGui::InputText("Server", serverHost, sizeof(serverHost));
    ImGui::InputInt("Port", &serverPort);
    ImGui::Separator();

    if (state == LoginScreenState::Login) {
        // === LOGIN FORM ===
        ImGui::InputText("Username", username, sizeof(username));
        ImGui::InputText("Password", password, sizeof(password), ImGuiInputTextFlags_Password);

        ImGui::Spacing();

        if (ImGui::Button("Login", ImVec2(-1, 30))) {
            // Validate
            if (!AuthValidation::isValidUsername(username)) {
                statusMessage = "Username: 3-20 chars, alphanumeric + underscore";
                isError = true;
            } else if (!AuthValidation::isValidPassword(password)) {
                statusMessage = "Password: 8-128 printable characters";
                isError = true;
            } else {
                loginSubmitted = true;
                statusMessage = "";
                isError = false;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Create Account", ImVec2(-1, 0))) {
            state = LoginScreenState::Register;
            statusMessage = "";
            isError = false;
        }

    } else {
        // === REGISTER FORM ===
        ImGui::InputText("Username", username, sizeof(username));
        ImGui::InputText("Email", email, sizeof(email));
        ImGui::InputText("Password", password, sizeof(password), ImGuiInputTextFlags_Password);
        ImGui::InputText("Confirm Password", confirmPassword, sizeof(confirmPassword), ImGuiInputTextFlags_Password);

        ImGui::Separator();
        ImGui::Text("Character");
        ImGui::InputText("Name", characterName, sizeof(characterName));

        ImGui::Text("Class:");
        ImGui::RadioButton("Warrior", &selectedClass, 0); ImGui::SameLine();
        ImGui::RadioButton("Mage", &selectedClass, 1); ImGui::SameLine();
        ImGui::RadioButton("Archer", &selectedClass, 2);

        ImGui::Spacing();
        ImGui::Text("Faction:");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        ImGui::RadioButton("Xyros", &selectedFaction, 0);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
        ImGui::RadioButton("Fenor", &selectedFaction, 1);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.80f, 0.40f, 1.0f));
        ImGui::RadioButton("Zethos", &selectedFaction, 2);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.20f, 1.0f));
        ImGui::RadioButton("Solis", &selectedFaction, 3);
        ImGui::PopStyleColor();

        ImGui::Spacing();

        if (ImGui::Button("Register", ImVec2(-1, 30))) {
            // Validate all fields
            if (!AuthValidation::isValidUsername(username)) {
                statusMessage = "Username: 3-20 chars, alphanumeric + underscore";
                isError = true;
            } else if (!AuthValidation::isValidEmail(email)) {
                statusMessage = "Please enter a valid email address";
                isError = true;
            } else if (!AuthValidation::isValidPassword(password)) {
                statusMessage = "Password: 8-128 printable characters";
                isError = true;
            } else if (std::string(password) != std::string(confirmPassword)) {
                statusMessage = "Passwords do not match";
                isError = true;
            } else if (!AuthValidation::isValidCharacterName(characterName)) {
                statusMessage = "Character name: 2-16 chars, alphanumeric + spaces";
                isError = true;
            } else {
                registerSubmitted = true;
                statusMessage = "";
                isError = false;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Back to Login", ImVec2(-1, 0))) {
            state = LoginScreenState::Login;
            statusMessage = "";
            isError = false;
        }
    }

    // Status message
    if (!statusMessage.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        if (isError) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        }
        ImGui::TextWrapped("%s", statusMessage.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

void LoginScreen::reset() {
    state = LoginScreenState::Login;
    std::memset(username, 0, sizeof(username));
    std::memset(password, 0, sizeof(password));
    std::memset(confirmPassword, 0, sizeof(confirmPassword));
    std::memset(email, 0, sizeof(email));
    std::memset(characterName, 0, sizeof(characterName));
    selectedClass = 0;
    selectedFaction = 0;
    statusMessage = "";
    isError = false;
    loginSubmitted = false;
    registerSubmitted = false;
}

} // namespace fate
