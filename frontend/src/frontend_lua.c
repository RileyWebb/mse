#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include <stdio.h>

#include "libmse/libmse.h"
#include "libmse/libmse_lua.h"
#include "libmse/libmse_log.h"
#include "frontend_file_dialog.h"

#include "frontend_ui.h"

// Example callback landing target
void on_script_selected(const char *chosen_path, void *userdata) {
    libmse_lua_worker_t *worker = (libmse_lua_worker_t *)userdata;
    
    if (chosen_path != NULL && worker != NULL) {
        libmse_logf("Dispatched script to worker: %s", chosen_path);
        libmse_lua_worker_execute_script(worker, chosen_path);
    }
}

void mse_frontend_ui_draw_lua_debugger(mse_frontend_ui_state_t *state) {
    // 1. Begin the ImGui Window
    if (!igBegin("Lua Runtime Debugger", &state->show_lua_debugger_window, 0)) {
        igEnd();
        return;
    }

    // 2. Fetch the registered background worker list and default worker
    libmse_lua_worker_t **workers = NULL;
    size_t worker_count = 0;
    libmse_lua_get_worker_list(&workers, &worker_count);

    libmse_lua_worker_t *active_worker = NULL;
    lua_State *L = NULL;
    
    // Allocate a persistent buffer for the dynamic combobox preview string
    static char selected_label[128];
    snprintf(selected_label, sizeof(selected_label), "None");

    // Fallback bounds safety check if the worker count drops dynamically
    if (state->selected_lua_worker_idx >= (int)worker_count) {
        state->selected_lua_worker_idx = -1; 
    }

    // Resolve the active target worker and lua_State pointer based on selection index
    if (workers && state->selected_lua_worker_idx >= 0) {
        active_worker = workers[state->selected_lua_worker_idx];
        if (active_worker) {
            snprintf(selected_label, sizeof(selected_label), "%s (Worker #%d)", 
                     active_worker->name ? active_worker->name : "Threaded Worker", state->selected_lua_worker_idx);
        }
    }

    if (active_worker) {
        L = active_worker->L;
    }

    // 3. Dropdown UI Selector for Target State
    igTextDisabled("Target Runtime Environment:");
    igPushItemWidth(-1.0f); // Stretch combo box completely to fill available width dynamically
    if (igBeginCombo("##TargetStateCombo", selected_label, 0)) {
        // Options: Registered Thread Workers List
        for (size_t i = 0; i < worker_count; i++) {
            if (workers[i] == NULL) continue;

            char item_name[128];
            snprintf(item_name, sizeof(item_name), "%s (Worker #%zu)##%p", 
                     workers[i]->name ? workers[i]->name : "Worker", i, (void*)workers[i]);
            
            bool is_selected = (state->selected_lua_worker_idx == (int)i);
            if (igSelectable_Bool(item_name, is_selected, 0, (ImVec2){0, 0})) {
                state->selected_lua_worker_idx = (int)i;
            }
        }
        igEndCombo();
    }
    igPopItemWidth();
    igSeparator();

    // 4. Worker Status & Control Dashboard
    if (active_worker != NULL) {
        // Metadata & Performance Indicators aligned using an explicit column offset to handle thin spacing cleanly
        igText("Thread Node:");
        igSameLine(130.0f, -1);
        igTextColored((ImVec4){0.3f, 0.75f, 1.0f, 1.0f}, active_worker->name ? active_worker->name : "Unnamed");
        
        igText("Execution:");
        igSameLine(130.0f, -1);
        igText(active_worker->is_threaded ? "Asynchronous" : "Synchronous / Inline");

        // Ready Indicator
        bool busy = libmse_lua_worker_is_busy(active_worker);
        igText("Status:");
        igSameLine(130.0f, -1);
        if (busy) {
            igTextColored((ImVec4){1.0f, 0.4f, 0.4f, 1.0f}, "BUSY");
        } else {
            igTextColored((ImVec4){0.4f, 1.0f, 0.4f, 1.0f}, "READY");
        }

        igSpacing();

        // Control Actions: Run File Action Banner (Full Width)
        if (igButton("Run Script...", (ImVec2){-1.0f, 24})) {
            frontend_file_dialog_open(NULL, on_script_selected, (void *)active_worker);
        }
        if (igIsItemHovered(0)) {
            igSetTooltip("%s", "Spawns a host path selection framework to run a raw .lua script file on this state context.");
        }

        igSpacing();

        // Command evaluation input row (Input field stretches, button stays pinned right)
        static char cmd_buf[1024] = "";
        igPushItemWidth(-102.0f); // Reserve exactly enough space on the right for the execute button + spacing
        igInputText("##LuaCmdInput", cmd_buf, sizeof(cmd_buf), 0, NULL, NULL);
        igPopItemWidth();

        igSameLine(0, 6);

        if (igButton("Execute", (ImVec2){96, 24})) {
            if (cmd_buf[0] != '\0') {
                libmse_lua_worker_execute_string(active_worker, cmd_buf);
            }
        }
        igSeparator();
    }

    // Safety guard if the chosen target state isn't initialized or missing
    if (L == NULL) {
        igTextDisabled("The selected Lua runtime is not initialized or unavailable.");
        igEnd();
        return;
    }

    // 5. Setup Tabs (Remains focused contextually on active 'L')
    if (igBeginTabBar("LuaDebugTabs", 0)) {

        if (igBeginTabItem("Memory & GC", NULL, 0)) {
            int kb = lua_gc(L, LUA_GCCOUNT, 0);
            int bytes = lua_gc(L, LUA_GCCOUNTB, 0);
            double total_mb = kb / 1024.0 + bytes / (1024.0 * 1024.0);

            igText("Managed Lua Memory: %.3f MB", total_mb);
            igSeparator();

            igTextDisabled("Garbage Collector Actions:");
            
            // Dynamic wrap-around logic for action buttons based on current window width
            ImGuiStyle* style = igGetStyle();
            float total_width = igGetContentRegionAvail().x;
            float current_row_width = 140.0f;

            // Button 1: Collect Garbage
            if (igButton("Collect Garbage", (ImVec2){140, 24})) {
                lua_gc(L, LUA_GCCOLLECT, 0);
            }

            // Button 2: Stop GC
            if (current_row_width + style->ItemSpacing.x + 100.0f <= total_width) {
                igSameLine(0, -1);
                current_row_width += style->ItemSpacing.x + 100.0f;
            } else {
                current_row_width = 100.0f; // Wrapped to a new line
            }
            if (igButton("Stop GC", (ImVec2){100, 24})) {
                lua_gc(L, LUA_GCSTOP, 0);
            }

            // Button 3: Restart GC
            if (current_row_width + style->ItemSpacing.x + 100.0f <= total_width) {
                igSameLine(0, -1);
                current_row_width += style->ItemSpacing.x + 100.0f;
            } else {
                current_row_width = 100.0f; // Wrapped to a new line
            }
            if (igButton("Restart GC", (ImVec2){100, 24})) {
                lua_gc(L, LUA_GCRESTART, 0);
            }

            igEndTabItem();
        }

        if (igBeginTabItem("Active Files / Modules", NULL, 0)) {
            igText("Modules currently cached in 'package.loaded':");
            igSpacing();

            // Added ImGuiTableFlags_Resizable to let users squash columns manually when tight on screen space
            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            
            if (igBeginTable("LoadedModulesTable", 2, flags, (ImVec2){0, 300}, 0)) {
                igTableSetupColumn("Module / File Key", 0, 0, 0);
                igTableSetupColumn("Value Type", 0, 0, 0);
                igTableHeadersRow();

                lua_getglobal(L, "package");
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "loaded");
                    if (lua_istable(L, -1)) {
                        
                        lua_pushnil(L); 
                        while (lua_next(L, -2) != 0) {
                            const char* module_name = lua_tostring(L, -2);
                            const char* value_type = lua_typename(L, lua_type(L, -1));

                            igTableNextRow(0, 0);
                            
                            igTableSetColumnIndex(0);
                            igText(module_name ? module_name : "Unknown Source");

                            igTableSetColumnIndex(1);
                            if (lua_isboolean(L, -1)) {
                                igTextColored((ImVec4){0.4f, 0.8f, 1.0f, 1.0f}, "Loaded (bool)");
                            } else {
                                igText(value_type);
                            }

                            lua_pop(L, 1); 
                        }
                    }
                    lua_pop(L, 1); 
                }
                lua_pop(L, 1); 
                
                igEndTable();
            }
            igEndTabItem();
        }

        if (igBeginTabItem("Execution Call Stack", NULL, 0)) {
            igText("Current Lua Activation Records:");
            igSeparator();

            lua_Debug ar;
            int level = 0;

            while (lua_getstack(L, level, &ar)) {
                lua_getinfo(L, "nSl", &ar);

                char frame_label[256];
                snprintf(frame_label, sizeof(frame_label), "Frame %d: %s()##%d", level, (ar.name ? ar.name : "anonymous"), level);

                if (igTreeNode_Str(frame_label)) {
                    igText("Source file: %s", ar.short_src);
                    igText("Line: %d", ar.currentline);
                    igText("Defined as: %s", ar.what);
                    if (ar.linedefined > 0) {
                        igText("Source range: lines %d to %d", ar.linedefined, ar.lastlinedefined);
                    }
                    igTreePop();
                }
                level++;
            }

            if (level == 0) {
                igTextDisabled("Lua execution engine is currently idle (No active call frames).");
            }

            igEndTabItem();
        }

        igEndTabBar();
    }

    igEnd();
}