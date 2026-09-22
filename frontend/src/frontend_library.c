#define DEBUG_LOG_SOURCE "frontend_library"
#include "frontend_ui.h"
#include "frontend_cimgui.h"
#include "frontend_imgui.h"
#include "frontend_icons.h"
#include "libmse/libmse_db.h"
#include "libmse/libmse_library.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Temporary globals linked from your main app
extern libmse_db_t *g_temp_db;
extern libmse_library_t *g_temp_lib;

typedef enum {
    LIB_VIEW_GRID,
    LIB_VIEW_LIST
} lib_view_mode_t;

typedef struct {
    lib_view_mode_t view_mode;
    char            search_query[256];
    char            add_rom_path[512];
    
    int64_t         selected_game_id;
    char            selected_rom_path[1024];
} library_view_state_t;

static library_view_state_t s_lib_state = { 
    .view_mode = LIB_VIEW_GRID, 
    .search_query = "", 
    .add_rom_path = "", 
    .selected_game_id = -1, 
    .selected_rom_path = "" 
};

void mse_frontend_library_view_init(void) {
    s_lib_state.selected_game_id = -1;
    memset(s_lib_state.search_query, 0, sizeof(s_lib_state.search_query));
}

static void draw_toolbar(void) {
    igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){mse_frontend_ui_px(8.0f), 0.0f});
    
    // Search Bar
    igSetNextItemWidth(mse_frontend_ui_px(250.0f));
    igInputTextWithHint("##LibSearch", MSE_ICON_SEARCH " Search library...", s_lib_state.search_query, sizeof(s_lib_state.search_query), 0, NULL, NULL);
    
    igSameLine(0, mse_frontend_ui_px(16.0f));
    
    // Add Game Input
    igSetNextItemWidth(mse_frontend_ui_px(280.0f));
    igInputTextWithHint("##LibAddPath", "ROM file path to add...", s_lib_state.add_rom_path, sizeof(s_lib_state.add_rom_path), 0, NULL, NULL);
    igSameLine(0, mse_frontend_ui_px(4.0f));
    if (igButton(MSE_ICON_ADD, (ImVec2){0, 0})) {
        if (g_temp_lib && s_lib_state.add_rom_path[0] != '\0') {
            libmse_library_add_game(g_temp_lib, s_lib_state.add_rom_path);
            s_lib_state.add_rom_path[0] = '\0'; // Clear on success
        }
    }
    if (igIsItemHovered(ImGuiHoveredFlags_None)) {
        igSetTooltip("Scan & Add ROM to Library");
    }

    // View toggles right-aligned
    const float toggle_width = mse_frontend_ui_px(68.0f);
    igSameLine(igGetContentRegionAvail().x - toggle_width, 0);
    
    igPushStyleColor_Vec4(ImGuiCol_Button, s_lib_state.view_mode == LIB_VIEW_GRID ? (ImVec4){0.38f, 0.20f, 0.72f, 0.6f} : (ImVec4){0,0,0,0});
    if (igButton(MSE_ICON_LIBRARY, (ImVec2){30.0f, 0})) s_lib_state.view_mode = LIB_VIEW_GRID;
    igPopStyleColor(1);
    
    igSameLine(0, 0);
    
    igPushStyleColor_Vec4(ImGuiCol_Button, s_lib_state.view_mode == LIB_VIEW_LIST ? (ImVec4){0.38f, 0.20f, 0.72f, 0.6f} : (ImVec4){0,0,0,0});
    if (igButton(MSE_ICON_MEMVIEW, (ImVec2){30.0f, 0})) s_lib_state.view_mode = LIB_VIEW_LIST;
    igPopStyleColor(1);

    igPopStyleVar(1);
    
    igDummy((ImVec2){0.0f, mse_frontend_ui_px(6.0f)});
    igSeparator();
    igDummy((ImVec2){0.0f, mse_frontend_ui_px(6.0f)});
}

static void draw_details_pane(mse_frontend_ui_state_t *state, ImVec2 size) {
    if (s_lib_state.selected_game_id == -1) return;

    if (mse_frontend_ui_begin_child_window("LIB_DETAILS_PANE", size, true)) {
        if (!g_temp_db) {
            mse_frontend_ui_end_child_window();
            return;
        }

        const char *sql = 
            "SELECT g.name, g.release_year, g.description, p.name, c.name, f.rom_path, length(g.artwork_blob) "
            "FROM games g "
            "LEFT JOIN platforms p ON g.platform_id = p.id "
            "LEFT JOIN companies c ON p.company_id = c.id "
            "JOIN game_files f ON g.id = f.game_id "
            "WHERE g.id = ?1 LIMIT 1;";

        libmse_stmt_t *stmt = libmse_db_stmt_prepare(g_temp_db, sql);
        if (stmt) {
            libmse_db_bind_int64(stmt, 1, s_lib_state.selected_game_id);
            if (libmse_db_stmt_step(stmt) == 1) { 
                const char *name = libmse_db_col_text(stmt, 0);
                int year         = libmse_db_col_int(stmt, 1);
                const char *desc = libmse_db_col_text(stmt, 2);
                const char *plat = libmse_db_col_text(stmt, 3);
                const char *comp = libmse_db_col_text(stmt, 4);
                const char *path = libmse_db_col_text(stmt, 5);
                int art_size     = libmse_db_col_int(stmt, 6);

                if (path) strncpy(s_lib_state.selected_rom_path, path, sizeof(s_lib_state.selected_rom_path)-1);

                // Beautiful Cover Art Hero Banner
                ImVec2 art_size_vec = { igGetContentRegionAvail().x, mse_frontend_ui_px(170.0f) };
                ImU32 art_bg = igGetColorU32_Vec4((ImVec4){0.15f, 0.12f, 0.22f, 1.0f});
                ImDrawList* draw_list = igGetWindowDrawList();
                ImVec2 cursor_pos = igGetCursorScreenPos();
                
                ImDrawList_AddRectFilled(draw_list, cursor_pos, (ImVec2){cursor_pos.x + art_size_vec.x, cursor_pos.y + art_size_vec.y}, art_bg, mse_frontend_ui_px(12.0f), 0);
                
                if (art_size > 0) {
                    igSetCursorScreenPos((ImVec2){cursor_pos.x + mse_frontend_ui_px(14.0f), cursor_pos.y + art_size_vec.y - mse_frontend_ui_px(32.0f)});
                    igTextColored((ImVec4){0.36f, 0.82f, 0.52f, 1.0f}, MSE_ICON_INSTALLED " Premium Artwork Extracted");
                } else {
                    ImFont *icon_font = mse_frontend_imgui_font_icon();
                    if (icon_font) {
                        ImDrawList_AddText_FontPtr(draw_list, icon_font, mse_frontend_imgui_font_size_icon() * 3.0f, (ImVec2){cursor_pos.x + art_size_vec.x * 0.5f - mse_frontend_ui_px(24.0f), cursor_pos.y + art_size_vec.y * 0.5f - mse_frontend_ui_px(32.0f)}, igGetColorU32_Vec4((ImVec4){0.30f, 0.25f, 0.40f, 0.8f}), MSE_ICON_LIBRARY, NULL, 0.0f, NULL);
                    }
                }
                igDummy(art_size_vec);
                igDummy((ImVec2){0.0f, mse_frontend_ui_px(10.0f)});
                
                // Typography Title
                igPushTextWrapPos(igGetContentRegionAvail().x);
                igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
                igTextColored((ImVec4){0.96f, 0.96f, 0.99f, 1.0f}, "%s", name ? name : "Unknown Game");
                igPopFont();
                igPopTextWrapPos();
                
                // Meta info
                igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
                igTextColored((ImVec4){0.78f, 0.66f, 1.00f, 1.0f}, "%s", plat ? plat : "Unknown Platform");
                igTextDisabled("%s  •  %d", comp ? comp : "Unknown Developer", year);
                igPopFont();

                igDummy((ImVec2){0.0f, mse_frontend_ui_px(16.0f)});
                
                // Play Button
                igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.42f, 0.30f, 0.88f, 0.8f});
                igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.52f, 0.40f, 1.00f, 0.9f});
                if (igButton(MSE_ICON_START_CORE " Launch Game", (ImVec2){-1.0f, mse_frontend_ui_px(42.0f)})) {
                    if (path) {
                        strncpy(state->rom_path, path, sizeof(state->rom_path)-1);
                        state->core_view_requested = true;
                    }
                }
                igPopStyleColor(2);

                igDummy((ImVec2){0.0f, mse_frontend_ui_px(16.0f)});
                igSeparator();
                igDummy((ImVec2){0.0f, mse_frontend_ui_px(8.0f)});

                // Game Description Box
                igPushTextWrapPos(igGetContentRegionAvail().x);
                igTextColored((ImVec4){0.78f, 0.80f, 0.86f, 1.0f}, "%s", desc ? desc : "No scraped description available for this title.");
                igPopTextWrapPos();

                igDummy((ImVec2){0.0f, mse_frontend_ui_px(16.0f)});
                
                // File info
                igTextDisabled("ROM Location:");
                igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
                igPushTextWrapPos(igGetContentRegionAvail().x);
                igTextColored((ImVec4){0.55f, 0.55f, 0.65f, 1.0f}, "%s", path ? path : "N/A");
                igPopTextWrapPos();
                igPopFont();
            }
            libmse_db_stmt_finalize(stmt);
        }
    }
    mse_frontend_ui_end_child_window();
}

static void draw_grid_view(libmse_stmt_t *stmt) {
    float avail_width = igGetContentRegionAvail().x;
    float card_width  = mse_frontend_ui_px(146.0f);
    float card_height = mse_frontend_ui_px(220.0f);
    float spacing     = mse_frontend_ui_px(20.0f);
    
    int cols = (int)(avail_width / (card_width + spacing));
    if (cols < 1) cols = 1;

    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody;
    if (igBeginTable("lib_grid", cols, flags, (ImVec2){0.0f, 0.0f}, 0.0f)) {
        
        while (libmse_db_stmt_step(stmt) == 1) { // 1 = SQLITE_ROW
            igTableNextColumn();
            
            int64_t id       = libmse_db_col_int64(stmt, 0);
            const char* name = libmse_db_col_text(stmt, 1);
            int year         = libmse_db_col_int(stmt, 2);
            int has_art      = libmse_db_col_int(stmt, 6) > 0;

            bool is_selected = (s_lib_state.selected_game_id == id);
            
            igPushID_Int((int)id);
            ImVec2 cursor_pos = igGetCursorScreenPos();
            
            // Invisible selectable zone
            igSelectable_Bool("##card_sel", is_selected, ImGuiSelectableFlags_AllowOverlap, (ImVec2){card_width, card_height});
            if (igIsItemClicked(ImGuiMouseButton_Left)) {
                s_lib_state.selected_game_id = id;
            }

            bool is_hovered = igIsItemHovered(ImGuiHoveredFlags_None);
            
            // Draw Premium Rounded Card
            ImDrawList* draw_list = igGetWindowDrawList();
            ImU32 bg_col   = igGetColorU32_Vec4(is_selected ? (ImVec4){0.32f, 0.20f, 0.58f, 0.8f} : (is_hovered ? (ImVec4){0.20f, 0.18f, 0.28f, 0.9f} : (ImVec4){0.12f, 0.11f, 0.16f, 0.7f}));
            ImU32 border_col = igGetColorU32_Vec4(is_selected ? (ImVec4){0.78f, 0.66f, 1.0f, 1.0f} : (is_hovered ? (ImVec4){0.5f, 0.4f, 0.8f, 0.6f} : (ImVec4){0.25f, 0.22f, 0.35f, 0.4f}));
            
            ImDrawList_AddRectFilled(draw_list, cursor_pos, (ImVec2){cursor_pos.x + card_width, cursor_pos.y + card_height}, bg_col, mse_frontend_ui_px(10.0f), 0);
            
            // Inner gradient shadow for cover art zone
            ImVec2 art_end = {cursor_pos.x + card_width, cursor_pos.y + card_height - mse_frontend_ui_px(60.0f)};
            ImU32 art_zone_bg = igGetColorU32_Vec4((ImVec4){0.08f, 0.07f, 0.12f, 0.9f});
            ImDrawList_AddRectFilled(draw_list, cursor_pos, art_end, art_zone_bg, mse_frontend_ui_px(10.0f), ImDrawFlags_RoundCornersTop);
            
            // Draw Box Border
            ImDrawList_AddRect(draw_list, cursor_pos, (ImVec2){cursor_pos.x + card_width, cursor_pos.y + card_height}, border_col, mse_frontend_ui_px(10.0f), 0, is_selected ? mse_frontend_ui_px(2.0f) : mse_frontend_ui_px(1.0f));

            if (has_art) {
                ImFont *icon_font = mse_frontend_imgui_font_icon();
                if (icon_font) {
                    ImDrawList_AddText_FontPtr(draw_list, icon_font, mse_frontend_imgui_font_size_icon() * 2.5f, (ImVec2){cursor_pos.x + card_width/2.0f - mse_frontend_ui_px(20.0f), cursor_pos.y + (card_height - 60.0f)/2.0f - mse_frontend_ui_px(20.0f)}, igGetColorU32_Vec4((ImVec4){0.36f, 0.82f, 0.52f, 0.4f}), MSE_ICON_LIBRARY, NULL, 0.0f, NULL);
                }
            } else {
                ImDrawList_AddText_Vec2(draw_list, (ImVec2){cursor_pos.x + card_width/2.0f - mse_frontend_ui_px(28.0f), cursor_pos.y + (card_height - 60.0f)/2.0f - mse_frontend_ui_px(8.0f)}, igGetColorU32_Vec4((ImVec4){0.4f, 0.4f, 0.45f, 0.6f}), "NO ART", NULL);
            }

            // Typography alignment inside card
            ImVec2 text_pos = {cursor_pos.x + mse_frontend_ui_px(10.0f), cursor_pos.y + card_height - mse_frontend_ui_px(52.0f)};
            ImFont *title_font = mse_frontend_imgui_font_body();
            if (title_font) {
                ImVec4 clip_rect = {text_pos.x, text_pos.y, text_pos.x + card_width - mse_frontend_ui_px(20.0f), text_pos.y + mse_frontend_ui_px(34.0f)};
                ImFont_RenderText(title_font, draw_list, mse_frontend_imgui_font_size_body(), text_pos, igGetColorU32_Vec4((ImVec4){0.92f, 0.92f, 0.96f, 1.0f}), clip_rect, name ? name : "Unknown", NULL, card_width - mse_frontend_ui_px(20.0f), true);
            }
            
            char year_buf[16];
            snprintf(year_buf, sizeof(year_buf), "%d", year);
            ImDrawList_AddText_Vec2(draw_list, (ImVec2){text_pos.x, text_pos.y + mse_frontend_ui_px(34.0f)}, igGetColorU32_Vec4((ImVec4){0.55f, 0.55f, 0.65f, 1.0f}), year_buf, NULL);

            igPopID();
        }
        igEndTable();
    }
}

static void draw_list_view(libmse_stmt_t *stmt) {
    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable;
    
    if (igBeginTable("lib_list_table", 5, flags, (ImVec2){0.0f, 0.0f}, 0.0f)) {
        igTableSetupScrollFreeze(0, 1);
        igTableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
        igTableSetupColumn("Platform", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(140.0f), 0);
        igTableSetupColumn("Year", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(60.0f), 0);
        igTableSetupColumn("File Path", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
        igTableSetupColumn("Art", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(50.0f), 0);
        igTableHeadersRow();

        while (libmse_db_stmt_step(stmt) == 1) {
            int64_t id       = libmse_db_col_int64(stmt, 0);
            const char* name = libmse_db_col_text(stmt, 1);
            int year         = libmse_db_col_int(stmt, 2);
            const char* plat = libmse_db_col_text(stmt, 3);
            const char* path = libmse_db_col_text(stmt, 5);
            int art_size     = libmse_db_col_int(stmt, 6);

            igTableNextRow(0, 0.0f);
            
            igTableSetColumnIndex(0);
            bool is_selected = (s_lib_state.selected_game_id == id);
            
            char label_id[64];
            snprintf(label_id, sizeof(label_id), "%s##%d", name ? name : "Unknown", (int)id);
            if (igSelectable_Bool(label_id, is_selected, ImGuiSelectableFlags_SpanAllColumns, (ImVec2){0,0})) {
                s_lib_state.selected_game_id = id;
            }
            
            igTableSetColumnIndex(1);
            igTextDisabled("%s", plat ? plat : "--");
            
            igTableSetColumnIndex(2);
            igText("%d", year);
            
            igTableSetColumnIndex(3);
            igTextColored((ImVec4){0.6f, 0.6f, 0.7f, 1.0f}, "%s", path ? path : "");

            igTableSetColumnIndex(4);
            if (art_size > 0) {
                igTextColored((ImVec4){0.36f, 0.82f, 0.52f, 1.0f}, MSE_ICON_INSTALLED);
            } else {
                igTextDisabled("-");
            }
        }
        igEndTable();
    }
}

void mse_frontend_library_view_draw(mse_frontend_ui_state_t *state) {
    const ImVec2 content_pos  = igGetCursorScreenPos();
    const ImVec2 content_size = igGetContentRegionAvail();
    const float  pad_x        = mse_frontend_ui_px(24.0f);
    const float  pad_y        = mse_frontend_ui_px(20.0f);

    igSetCursorScreenPos((ImVec2){content_pos.x + pad_x, content_pos.y + pad_y});

    // Header
    igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
    igTextColored((ImVec4){0.90f, 0.90f, 0.95f, 1.0f}, "LIBRARY");
    igPopFont();
    igTextDisabled("Manage your scraped ROMs and artwork collection.");
    igSpacing();
    
    // Main UI Box containing everything
    ImVec2 main_area_size = { content_size.x - pad_x * 2.0f, content_size.y - pad_y - mse_frontend_ui_px(58.0f) };
    if (mse_frontend_ui_begin_child_window("LIB_MAIN_CHILD", main_area_size, true)) {
        
        draw_toolbar();
        
        // Calculate dynamic dual-pane split width based on selection
        float list_width = igGetContentRegionAvail().x;
        float details_width = mse_frontend_ui_px(340.0f); // Sleeker right pane
        
        if (s_lib_state.selected_game_id != -1 && list_width > mse_frontend_ui_px(650.0f)) {
            list_width -= (details_width + mse_frontend_ui_px(20.0f));
        } else {
            details_width = 0.0f; // Window too small to show right pane alongside
        }

        if (igBeginChild_Str("LIB_DATA_PANE", (ImVec2){list_width, 0.0f}, false, 0)) {
            if (g_temp_db) {
                // Build dynamic query based on search
                char sql[1024];
                if (strlen(s_lib_state.search_query) > 0) {
                    snprintf(sql, sizeof(sql), 
                        "SELECT g.id, g.name, g.release_year, p.name, c.name, f.rom_path, length(g.artwork_blob) "
                        "FROM games g "
                        "LEFT JOIN platforms p ON g.platform_id = p.id "
                        "LEFT JOIN companies c ON p.company_id = c.id "
                        "JOIN game_files f ON g.id = f.game_id "
                        "WHERE g.name LIKE '%%%s%%' ORDER BY g.name ASC;", s_lib_state.search_query);
                } else {
                    snprintf(sql, sizeof(sql), 
                        "SELECT g.id, g.name, g.release_year, p.name, c.name, f.rom_path, length(g.artwork_blob) "
                        "FROM games g "
                        "LEFT JOIN platforms p ON g.platform_id = p.id "
                        "LEFT JOIN companies c ON p.company_id = c.id "
                        "JOIN game_files f ON g.id = f.game_id "
                        "ORDER BY g.name ASC;");
                }
                
                libmse_stmt_t *stmt = libmse_db_stmt_prepare(g_temp_db, sql);
                if (stmt) {
                    if (s_lib_state.view_mode == LIB_VIEW_GRID) {
                        draw_grid_view(stmt);
                    } else {
                        draw_list_view(stmt);
                    }
                    libmse_db_stmt_finalize(stmt);
                } else {
                    igTextColored((ImVec4){1.0f, 0.4f, 0.4f, 1.0f}, "Database not initialized or query failed.");
                }
            } else {
                igTextDisabled("Database not currently loaded.");
            }
        }
        igEndChild();

        // Details Pane (Right side)
        if (details_width > 0.0f) {
            igSameLine(0, mse_frontend_ui_px(20.0f));
            draw_details_pane(state, (ImVec2){details_width, 0.0f});
        }
    }
    mse_frontend_ui_end_child_window();
}