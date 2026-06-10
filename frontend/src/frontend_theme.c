#include "frontend_theme.h"

#include "frontend_cimgui.h"

static void mse_frontend_theme_apply_retro(void) {
    ImGuiStyle *style = igGetStyle();

    igStyleColorsClassic(NULL);

    style->WindowRounding = 6.0f;
    style->FrameRounding = 4.0f;
    style->GrabRounding = 4.0f;
    style->ScrollbarRounding = 6.0f;
    style->WindowBorderSize = 1.0f;
    style->FrameBorderSize = 1.0f;

    style->Colors[ImGuiCol_WindowBg].x = 0.10f;
    style->Colors[ImGuiCol_WindowBg].y = 0.11f;
    style->Colors[ImGuiCol_WindowBg].z = 0.14f;
    style->Colors[ImGuiCol_WindowBg].w = 1.0f;

    style->Colors[ImGuiCol_Header].x = 0.20f;
    style->Colors[ImGuiCol_Header].y = 0.42f;
    style->Colors[ImGuiCol_Header].z = 0.62f;
    style->Colors[ImGuiCol_Header].w = 1.0f;

    style->Colors[ImGuiCol_Button].x = 0.16f;
    style->Colors[ImGuiCol_Button].y = 0.23f;
    style->Colors[ImGuiCol_Button].z = 0.34f;
    style->Colors[ImGuiCol_Button].w = 1.0f;
}

static void mse_frontend_theme_apply_mse(void) {
    ImGuiStyle *style = igGetStyle();

    igStyleColorsDark(NULL);

    style->WindowRounding = 0.0f;
    style->ChildRounding = 0.0f;
    style->FrameRounding = 0.0f;
    style->GrabRounding = 0.0f;
    style->PopupRounding = 0.0f;
    style->ScrollbarRounding = 0.0f;
    
    style->WindowBorderSize = 1.0f;
    style->ChildBorderSize = 1.0f;
    style->PopupBorderSize = 1.0f;
    style->FrameBorderSize = 1.0f;

    // Background: #131313
    style->Colors[ImGuiCol_WindowBg] = (ImVec4){ 0.075f, 0.075f, 0.075f, 1.0f }; // #131313
    style->Colors[ImGuiCol_ChildBg] = (ImVec4){ 0.075f, 0.075f, 0.075f, 1.0f };

    // Borders: #393939
    style->Colors[ImGuiCol_Border] = (ImVec4){ 0.224f, 0.224f, 0.224f, 1.0f };
    style->Colors[ImGuiCol_BorderShadow] = (ImVec4){ 0.0f, 0.0f, 0.0f, 0.0f };
    style->Colors[ImGuiCol_Separator] = (ImVec4){ 0.224f, 0.224f, 0.224f, 1.0f };

    // Primary Accent: #a371f7
    ImVec4 accent = (ImVec4){ 0.639f, 0.443f, 0.969f, 1.0f }; // #a371f7
    ImVec4 accent_hover = (ImVec4){ 0.739f, 0.543f, 1.0f, 1.0f };
    ImVec4 accent_active = (ImVec4){ 0.539f, 0.343f, 0.869f, 1.0f };

    style->Colors[ImGuiCol_Button] = (ImVec4){ 0.15f, 0.15f, 0.15f, 1.0f };
    style->Colors[ImGuiCol_ButtonHovered] = (ImVec4){ 0.2f, 0.2f, 0.2f, 1.0f };
    style->Colors[ImGuiCol_ButtonActive] = (ImVec4){ 0.25f, 0.25f, 0.25f, 1.0f };

    style->Colors[ImGuiCol_Header] = accent;
    style->Colors[ImGuiCol_HeaderHovered] = accent_hover;
    style->Colors[ImGuiCol_HeaderActive] = accent_active;

    style->Colors[ImGuiCol_FrameBg] = (ImVec4){ 0.15f, 0.15f, 0.15f, 1.0f };
    style->Colors[ImGuiCol_FrameBgHovered] = (ImVec4){ 0.2f, 0.2f, 0.2f, 1.0f };
    style->Colors[ImGuiCol_FrameBgActive] = (ImVec4){ 0.25f, 0.25f, 0.25f, 1.0f };
    
    style->Colors[ImGuiCol_Tab] = (ImVec4){ 0.1f, 0.1f, 0.1f, 1.0f };
    style->Colors[ImGuiCol_TabHovered] = accent_hover;
    //style->Colors[ImGuiCol_TabActive] = accent;
    //style->Colors[ImGuiCol_TabUnfocused] = (ImVec4){ 0.1f, 0.1f, 0.1f, 1.0f };
    //style->Colors[ImGuiCol_TabUnfocusedActive] = (ImVec4){ 0.15f, 0.15f, 0.15f, 1.0f };
    
    style->Colors[ImGuiCol_TitleBg] = (ImVec4){ 0.075f, 0.075f, 0.075f, 1.0f };
    style->Colors[ImGuiCol_TitleBgActive] = (ImVec4){ 0.075f, 0.075f, 0.075f, 1.0f };
    style->Colors[ImGuiCol_TitleBgCollapsed] = (ImVec4){ 0.075f, 0.075f, 0.075f, 1.0f };

    style->Colors[ImGuiCol_Text] = (ImVec4){ 0.9f, 0.9f, 0.9f, 1.0f };
}

const char *mse_frontend_theme_name(mse_frontend_theme_t theme) {
    switch (theme) {
    case MSE_FRONTEND_THEME_LIGHT:
        return "Light";
    case MSE_FRONTEND_THEME_CLASSIC:
        return "Classic";
    case MSE_FRONTEND_THEME_RETRO:
        return "Retro";
    case MSE_FRONTEND_THEME_MSE:
        return "MSE";
    case MSE_FRONTEND_THEME_DARK:
    default:
        return "Dark";
    }
}

mse_frontend_theme_t mse_frontend_theme_next(mse_frontend_theme_t theme) {
    switch (theme) {
    case MSE_FRONTEND_THEME_DARK:
        return MSE_FRONTEND_THEME_LIGHT;
    case MSE_FRONTEND_THEME_LIGHT:
        return MSE_FRONTEND_THEME_CLASSIC;
    case MSE_FRONTEND_THEME_CLASSIC:
        return MSE_FRONTEND_THEME_RETRO;
    case MSE_FRONTEND_THEME_RETRO:
        return MSE_FRONTEND_THEME_MSE;
    case MSE_FRONTEND_THEME_MSE:
    default:
        return MSE_FRONTEND_THEME_DARK;
    }
}

void mse_frontend_theme_apply(mse_frontend_theme_t theme) {
    switch (theme) {
    case MSE_FRONTEND_THEME_LIGHT:
        igStyleColorsLight(NULL);
        break;
    case MSE_FRONTEND_THEME_CLASSIC:
        igStyleColorsClassic(NULL);
        break;
    case MSE_FRONTEND_THEME_RETRO:
        mse_frontend_theme_apply_retro();
        break;
    case MSE_FRONTEND_THEME_MSE:
        mse_frontend_theme_apply_mse();
        break;
    case MSE_FRONTEND_THEME_DARK:
    default:
        igStyleColorsDark(NULL);
        break;
    }
}