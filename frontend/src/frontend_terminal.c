#define DEBUG_LOG_SOURCE "frontend"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "frontend_ui.h"
#include "frontend_imgui.h"
#include "frontend_cimgui.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_cmd.h"
#include "libmse/libmse_debug.h"
#include "frontend_app.h"

#define COLOR_ERROR (ImVec4){0.95f, 0.35f, 0.35f, 1.0f}
#define COLOR_SUCCESS (ImVec4){0.52f, 0.95f, 0.52f, 1.0f}
#define COLOR_INFO (ImVec4){0.62f, 0.52f, 0.96f, 1.0f}
#define COLOR_ECHO (ImVec4){0.74f, 0.74f, 0.78f, 1.0f}
#define COLOR_MATCH (ImVec4){0.80f, 0.80f, 0.80f, 1.0f}
#define COLOR_TEXT (ImVec4){0.75f, 0.75f, 0.78f, 1.0f}
#define COLOR_BG (ImVec4){0.0745098039f, 0.0745098039f, 0.0745098039f, 1.0f}

#define MAX_TERMINAL_HISTORY 512
#define MAX_COMMAND_HISTORY 64
#define INPUT_BUFFER_SIZE 512

typedef struct {
    char   text[512];
    ImVec4 color;
} TerminalLine;

// --- Autocomplete ---------------------------------------------------------
//
// The suggestion list is rebuilt from the registries whenever the input is
// edited, and drawn as a borrowed window under the input rather than a real
// popup: an ImGui popup takes focus, and a completion menu that stops you
// typing is worse than none.
//
// Names are copied rather than pointed at. The registries do own their
// strings, but a cvar can be destroyed between the frame that built this list
// and the frame that draws it, and a menu is not worth a dangling pointer.

#define MAX_SUGGESTIONS       32
#define SUGGESTION_NAME_MAX   64
#define SUGGESTION_DETAIL_MAX 128
#define SUGGESTION_VISIBLE    8 // rows on screen before the list scrolls

typedef struct {
    char name[SUGGESTION_NAME_MAX];
    char detail[SUGGESTION_DETAIL_MAX];
    bool is_cvar;
} TerminalSuggestion;

static TerminalSuggestion g_suggestions[MAX_SUGGESTIONS];
static int                g_suggestion_count = 0;
static int                g_suggestion_index = 0;
static int                g_suggestion_total = 0; // matches found, may exceed the array
static bool               g_suggestions_dirty = true;

// Set when a row is clicked; applied inside the input's callback, which is the
// only place the buffer can be edited safely while ImGui owns it.
static bool g_apply_completion = false;
static bool g_refocus_input    = false;

// Where the input box landed this frame, so the list can be hung underneath it
// after the console window has been closed out.
static ImVec2 g_input_min      = {0.0f, 0.0f};
static ImVec2 g_input_max      = {0.0f, 0.0f};
static bool   g_input_active   = false;
static bool   g_list_hovered   = false;

// Output display state
static TerminalLine g_terminal_history[MAX_TERMINAL_HISTORY];
static size_t       g_history_head                    = 0;
static size_t       g_history_size                    = 0;
static bool         g_scroll_to_bottom                = false;
static char         g_input_buffer[INPUT_BUFFER_SIZE] = {0};

// Command input history state
static char   g_command_history[MAX_COMMAND_HISTORY][INPUT_BUFFER_SIZE];
static size_t g_command_history_count                  = 0;
static int    g_command_history_pos                    = -1;
static char   g_command_temp_buffer[INPUT_BUFFER_SIZE] = "";

static void get_ansi_16_color(int code, bool intense, int *r, int *g, int *b) {
    int base = intense ? 255 : 170;
    int low  = intense ? 85  : 0;

    switch (code % 10) {
        case 0: *r = low;  *g = low;  *b = low;  break; // Black / Dark Gray
        case 1: *r = base; *g = low;  *b = low;  break; // Red
        case 2: *r = low;  *g = base; *b = low;  break; // Green
        case 3: *r = base; *g = base; *b = low;  break; // Yellow
        case 4: *r = low;  *g = low;  *b = base; break; // Blue
        case 5: *r = base; *g = low;  *b = base; break; // Magenta
        case 6: *r = low;  *g = base; *b = base; break; // Cyan
        case 7: *r = base; *g = base; *b = base; break; // White
        default: *r = 255; *g = 255; *b = 255; break;
    }
}

const char* ansi_color_parser(const char* start, const char* end, ImVec4 *color)
{
    if (!start || !end || start >= end)
        return end;

    // 1. Check for the ANSI Escape sequence prefix: '\033[' or '\x1b['
    if (start[0] != '\033' && start[0] != '\x1b') return start + 1;
    if ((start + 1 >= end) || start[1] != '[') return start + 1;

    const char *p = start + 2; // Move past '\x1b['

    // Keep track of text attributes
    bool intense = false;
    int r = 255, g = 255, b = 255; // Default fallback to white
    bool color_changed = false;

    // 2. Parse semicolon-separated integer parameters
    while (p < end && *p != 'm') {
        // Skip semicolons or unexpected characters
        if (*p == ';' || *p == ' ') {
            p++;
            continue;
        }

        // Read the next integer parameter
        if (*p >= '0' && *p <= '9') {
            char *next_p;
            int param = (int)strtol(p, &next_p, 10);
            p = next_p;

            // 3. Handle standard SGR commands
            if (param == 0) {
                // Reset everything
                intense = false;
                r = 255; g = 255; b = 255;
                color_changed = true;
            } 
            else if (param == 1) {
                // Bold / Intense modifier
                intense = true;
            } 
            else if (param >= 30 && param <= 37) {
                // Foreground standard 8 colors
                get_ansi_16_color(param, intense, &r, &g, &b);
                color_changed = true;
            } 
            else if (param >= 90 && param <= 97) {
                // Foreground high-intensity 8 colors
                get_ansi_16_color(param, true, &r, &g, &b);
                color_changed = true;
            }
        } else {
            // Unrecognized character inside the code block, abort to avoid infinite loop
            break;
        }
    }

    // Advance past the trailing command character 'm' if we hit it safely
    if (p < end && *p == 'm') {
        p++;
    }

    // 4. Update the ImGui color structure if we pulled a valid modification out
    if (color_changed && color) {
        *color = (ImVec4){r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
    }

    return p; // Return pointer right after 'm'
}

static void terminal_log(const char *text, ImVec4 color)
{
    strncpy(g_terminal_history[g_history_head].text, text, sizeof(g_terminal_history[g_history_head].text) - 1);
    g_terminal_history[g_history_head].text[sizeof(g_terminal_history[g_history_head].text) - 1] = '\0';
    g_terminal_history[g_history_head].color                                                     = color;

    g_history_head = (g_history_head + 1) % MAX_TERMINAL_HISTORY;
    if (g_history_size < MAX_TERMINAL_HISTORY) g_history_size++;
    g_scroll_to_bottom = true;
}

static const char *g_debug_log_strings[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "ASSRT"};

// --- REWRITTEN CALLBACK ---
// Implements libmse_log_callback signature. 
// Strips ANSI codes while extracting the core color for the UI row.
void mse_frontend_terminal_log_callback(const char *message)
{
    if (!message) return;

    char clean_text[INPUT_BUFFER_SIZE];
    ImVec4 row_color = COLOR_TEXT; // Fallback to standard interface text color
    
    const char *read_ptr = message;
    const char *end_ptr = message + strlen(message);
    char *write_ptr = clean_text;
    
    bool color_applied = false;

    // Single-pass parsing: Strip ANSI codes and grab the dominant color
    while (read_ptr < end_ptr && (write_ptr - clean_text) < (int)sizeof(clean_text) - 1) {
        if (*read_ptr == '\033' || *read_ptr == '\x1b') {
            ImVec4 temp_color;
            const char *next_ptr = ansi_color_parser(read_ptr, end_ptr, &temp_color);
            
            // Capture the first non-reset color we find to theme this terminal row.
            // We ignore subsequent codes (like \x1b[0m resets) so the row stays the designated color.
            if (!color_applied && next_ptr > read_ptr + 1) {
                row_color = temp_color;
                color_applied = true;
            }
            
            // Advance the read pointer past the escape sequence block
            read_ptr = next_ptr;
        } else {
            // Copy standard text
            *write_ptr++ = *read_ptr++;
        }
    }
    
    *write_ptr = '\0';
    
    // Dispatch the stripped text and parsed color to the history renderer
    terminal_log(clean_text, row_color);
}

static void terminal_push_command_history(const char *cmd)
{
    if (cmd[0] == '\0') return;
    if (g_command_history_count > 0 && strcmp(g_command_history[g_command_history_count - 1], cmd) == 0) return;

    if (g_command_history_count >= MAX_COMMAND_HISTORY) {
        for (size_t i = 0; i < MAX_COMMAND_HISTORY - 1; i++) strcpy(g_command_history[i], g_command_history[i + 1]);
        g_command_history_count = MAX_COMMAND_HISTORY - 1;
    }
    strncpy(g_command_history[g_command_history_count], cmd, INPUT_BUFFER_SIZE - 1);
    g_command_history[g_command_history_count][INPUT_BUFFER_SIZE - 1] = '\0';
    g_command_history_count++;
}

static bool cmd_clear_handler(int argc, const char** argv)
{
    g_history_head = 0;
    g_history_size = 0;
    return true;
}

static bool cmd_exit_handler(int argc, const char** argv)
{
    mse_frontend_quit();
    return true;
}

void mse_frontend_terminal_init(void)
{
    libmse_log_register_callback(mse_frontend_terminal_log_callback);

    libmse_cmd_register(&(libmse_cmd_t){"clear", "Flushes active history array lines", 0, cmd_clear_handler});
    libmse_cmd_register(&(libmse_cmd_t){"exit", "Exits the application immediately", 0, cmd_exit_handler});
    libmse_cmd_parse("alias quit exit");
}

// The prefix being completed: the first word of the line. Returns its length,
// and writes its offset to *start_out.
//
// Completion stops once a space has been typed after that word -- past it the
// line is arguments, and the registries have nothing to say about those.
static int terminal_completion_word(const char *buffer, int *start_out)
{
    int start = 0;
    while (buffer[start] == ' ') start++;

    int end = start;
    while (buffer[end] != '\0' && buffer[end] != ' ') end++;

    if (buffer[end] == ' ') {
        return -1;
    }

    *start_out = start;
    return end - start;
}

static bool terminal_prefix_match(const char *name, const char *prefix, int prefix_len)
{
    for (int i = 0; i < prefix_len; ++i) {
        // Case-insensitive, because remembering whether a cvar shouted is not
        // part of the job. Done by hand: strncasecmp is not standard C.
        char a = name[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b || a == '\0') {
            return false;
        }
    }
    return true;
}

// Inserts in alphabetical order, dropping anything past the array. Sorted on
// the way in because the registries hand these over in registration order,
// which is meaningless to read.
static void terminal_add_suggestion(const char *name, const char *detail, bool is_cvar)
{
    g_suggestion_total++;

    int position = g_suggestion_count;
    while (position > 0 && strcmp(g_suggestions[position - 1].name, name) > 0) {
        position--;
    }

    if (position >= MAX_SUGGESTIONS) {
        return;
    }

    const int last = (g_suggestion_count < MAX_SUGGESTIONS) ? g_suggestion_count : MAX_SUGGESTIONS - 1;
    for (int i = last; i > position; --i) {
        g_suggestions[i] = g_suggestions[i - 1];
    }

    TerminalSuggestion *slot = &g_suggestions[position];
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    snprintf(slot->detail, sizeof(slot->detail), "%s", detail ? detail : "");
    slot->is_cvar = is_cvar;

    if (g_suggestion_count < MAX_SUGGESTIONS) {
        g_suggestion_count++;
    }
}

typedef struct {
    const char *prefix;
    int         prefix_len;
} AutocompleteState;

static void autocomplete_cvar_callback(libmse_cvar_t *cvar, void *user_data)
{
    AutocompleteState *state = (AutocompleteState *)user_data;
    if (cvar->name == NULL || !terminal_prefix_match(cvar->name, state->prefix, state->prefix_len)) {
        return;
    }

    // The current value leads the description: when you are completing a cvar
    // you are usually about to change it, and what it is now is the thing you
    // wanted to know.
    char detail[SUGGESTION_DETAIL_MAX];
    switch (cvar->type) {
        case LIBMSE_CVAR_INT:
            snprintf(detail, sizeof(detail), "= %d", cvar->data.i ? *cvar->data.i : 0);
            break;
        case LIBMSE_CVAR_FLOAT:
            snprintf(detail, sizeof(detail), "= %.3f", cvar->data.f ? *cvar->data.f : 0.0f);
            break;
        case LIBMSE_CVAR_DOUBLE:
            snprintf(detail, sizeof(detail), "= %.3f", cvar->data.d ? *cvar->data.d : 0.0);
            break;
        case LIBMSE_CVAR_STRING:
            snprintf(detail, sizeof(detail), "= \"%s\"",
                     (cvar->data.s && *cvar->data.s) ? *cvar->data.s : "");
            break;
        default:
            detail[0] = '\0';
            break;
    }

    if (cvar->description != NULL && cvar->description[0] != '\0') {
        const size_t used = strlen(detail);
        snprintf(detail + used, sizeof(detail) - used, "   %s", cvar->description);
    }

    terminal_add_suggestion(cvar->name, detail, true);
}

static void autocomplete_cmd_callback(const libmse_cmd_t *cmd, void *user_data)
{
    AutocompleteState *state = (AutocompleteState *)user_data;
    if (cmd->name == NULL || !terminal_prefix_match(cmd->name, state->prefix, state->prefix_len)) {
        return;
    }

    terminal_add_suggestion(cmd->name, cmd->description, false);
}

static void terminal_rebuild_suggestions(const char *buffer)
{
    g_suggestion_count = 0;
    g_suggestion_total = 0;

    int       word_start = 0;
    const int word_len   = terminal_completion_word(buffer, &word_start);
    if (word_len <= 0) {
        g_suggestion_index = 0;
        return;
    }

    AutocompleteState state = {buffer + word_start, word_len};
    libmse_cmd_iterate(autocomplete_cmd_callback, &state);
    libmse_cvar_iterate(autocomplete_cvar_callback, &state);

    if (g_suggestion_index >= g_suggestion_count) {
        g_suggestion_index = 0;
    }
}

// Replaces the word under construction with `name`, plus the space you would
// have typed next. Only legal from inside an input callback.
static void terminal_apply_completion(ImGuiInputTextCallbackData *data, const char *name)
{
    int       word_start = 0;
    const int word_len   = terminal_completion_word(data->Buf, &word_start);
    if (word_len < 0) {
        return;
    }

    char replacement[SUGGESTION_NAME_MAX + 2];
    snprintf(replacement, sizeof(replacement), "%s ", name);

    ImGuiInputTextCallbackData_DeleteChars(data, word_start, word_len);
    ImGuiInputTextCallbackData_InsertChars(data, word_start, replacement, NULL);

    // The line now has a trailing space, so the next rebuild finds no word to
    // complete and the list closes on its own.
    g_suggestion_count  = 0;
    g_suggestion_total  = 0;
    g_suggestion_index  = 0;
    g_suggestions_dirty = false;
}

static int terminal_input_callback(ImGuiInputTextCallbackData *data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        // A row was clicked. The edit has to happen here because ImGui owns
        // the buffer while the field is active.
        if (g_apply_completion) {
            g_apply_completion = false;
            if (g_suggestion_index >= 0 && g_suggestion_index < g_suggestion_count) {
                terminal_apply_completion(data, g_suggestions[g_suggestion_index].name);
            }
        }
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
        g_suggestions_dirty = true;
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        // While the menu is up the arrows belong to it. Command history is
        // still on the arrows once it closes, which is the state you are in
        // whenever the line is empty.
        if (g_suggestion_count > 0) {
            if (data->EventKey == ImGuiKey_UpArrow) {
                g_suggestion_index = (g_suggestion_index + g_suggestion_count - 1) % g_suggestion_count;
            } else if (data->EventKey == ImGuiKey_DownArrow) {
                g_suggestion_index = (g_suggestion_index + 1) % g_suggestion_count;
            }
            return 0;
        }

        int prev_pos = g_command_history_pos;

        if (data->EventKey == ImGuiKey_UpArrow) {
            if (g_command_history_pos == -1 && g_command_history_count > 0) {
                strncpy(g_command_temp_buffer, data->Buf, INPUT_BUFFER_SIZE - 1);
                g_command_history_pos = (int)g_command_history_count - 1;
            } else if (g_command_history_pos > 0)
                g_command_history_pos--;
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (g_command_history_pos != -1) {
                if ((size_t)g_command_history_pos < g_command_history_count - 1)
                    g_command_history_pos++;
                else
                    g_command_history_pos = -1;
            }
        }

        if (prev_pos != g_command_history_pos) {
            const char *target_str =
                (g_command_history_pos == -1) ? g_command_temp_buffer : g_command_history[g_command_history_pos];
            snprintf(data->Buf, (size_t)data->BufSize, "%s", target_str);
            data->BufTextLen = (int)strlen(data->Buf);
            data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
            data->BufDirty                                              = true;
        }
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        // Tab takes whatever the menu has highlighted, which for an untouched
        // menu is the first row -- so Tab on a unique prefix still just
        // completes it.
        if (g_suggestion_index >= 0 && g_suggestion_index < g_suggestion_count) {
            terminal_apply_completion(data, g_suggestions[g_suggestion_index].name);
        }
    }
    return 0;
}

// The suggestion list, hung under the input box.
//
// Drawn as a window of its own rather than a child of the console so it can
// overflow the console's bounds, and with NoFocusOnAppearing so that opening
// it does not take the keyboard away from the input it is attached to.
static void terminal_draw_suggestions(void)
{
    if (g_suggestion_count == 0 || (!g_input_active && !g_list_hovered)) {
        g_list_hovered = false;
        return;
    }

    const float line_height = igGetTextLineHeightWithSpacing();
    const int   visible     = (g_suggestion_count < SUGGESTION_VISIBLE) ? g_suggestion_count : SUGGESTION_VISIBLE;
    const float padding     = igGetStyle()->WindowPadding.y * 2.0f;
    const bool  truncated   = g_suggestion_total > g_suggestion_count;

    float height = (float)visible * line_height + padding;
    if (truncated) {
        height += line_height;
    }

    igSetNextWindowPos((ImVec2){g_input_min.x, g_input_max.y + mse_frontend_ui_px(2.0f)}, ImGuiCond_Always,
                       (ImVec2){0.0f, 0.0f});
    igSetNextWindowSize((ImVec2){g_input_max.x - g_input_min.x, height}, ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;

    igPushStyleColor_Vec4(ImGuiCol_WindowBg, (ImVec4){0.10f, 0.09f, 0.14f, 0.98f});
    igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0.42f, 0.30f, 0.68f, 0.60f});
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, mse_frontend_ui_px(1.0f));
    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, mse_frontend_ui_px(6.0f));

    if (igBegin("##MSE_CONSOLE_SUGGESTIONS", NULL, flags)) {
        g_list_hovered = igIsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        const float name_column = mse_frontend_ui_px(190.0f);

        for (int i = 0; i < g_suggestion_count; i++) {
            const TerminalSuggestion *suggestion = &g_suggestions[i];

            igPushID_Int(i);
            if (igSelectable_Bool("##row", i == g_suggestion_index,
                                  ImGuiSelectableFlags_AllowOverlap, (ImVec2){0.0f, 0.0f})) {
                g_suggestion_index = i;
                g_apply_completion = true;
                g_refocus_input    = true;
            }

            // Keeps the highlighted row on screen while the arrows move it.
            if (i == g_suggestion_index && igIsWindowAppearing()) {
                igSetScrollHereY(0.5f);
            }

            igSameLine(mse_frontend_ui_px(6.0f), 0.0f);
            igTextColored(suggestion->is_cvar ? (ImVec4){0.62f, 0.82f, 1.0f, 1.0f} : COLOR_SUCCESS, "%s",
                          suggestion->name);

            igSameLine(name_column, 0.0f);
            igTextColored((ImVec4){0.50f, 0.50f, 0.55f, 1.0f}, "%s", suggestion->is_cvar ? "cvar" : "cmd");

            if (suggestion->detail[0] != '\0') {
                igSameLine(name_column + mse_frontend_ui_px(42.0f), 0.0f);
                igTextColored(COLOR_MATCH, "%s", suggestion->detail);
            }
            igPopID();
        }

        if (truncated) {
            igTextColored((ImVec4){0.50f, 0.50f, 0.55f, 1.0f}, "  ... %d more",
                          g_suggestion_total - g_suggestion_count);
        }
    } else {
        g_list_hovered = false;
    }
    igEnd();

    igPopStyleVar(2);
    igPopStyleColor(2);
}

static void terminal_execute_command(const char *cmd_line)
{
    libmse_cmd_parse(cmd_line);
}

// Main UI Rendering Block
void mse_frontend_ui_draw_terminal(mse_frontend_ui_state_t *state)
{
    if (state == NULL || !state->show_terminal) return;

    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, mse_frontend_ui_px(14.0f));
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, mse_frontend_ui_px(1.0f));
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){mse_frontend_ui_px(8.0f), mse_frontend_ui_px(8.0f)});

    igPushStyleColor_Vec4(ImGuiCol_WindowBg, COLOR_BG);
    igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0.42f, 0.30f, 0.68f, 0.42f});
    igPushStyleColor_Vec4(ImGuiCol_TitleBg, (ImVec4){0.16f, 0.12f, 0.28f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_TitleBgActive, (ImVec4){0.22f, 0.15f, 0.42f, 1.0f});

    igSetNextWindowSize((ImVec2){mse_frontend_ui_px(520.0f), mse_frontend_ui_px(360.0f)}, ImGuiCond_FirstUseEver);

    bool terminal_open = state->show_terminal != 0;
    const bool terminal_visible = igBegin("MSE_CONSOLE", &terminal_open,
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
    state->show_terminal = terminal_open ? 1 : 0;

    if (terminal_visible) {

        float footer_height_to_reserve = igGetStyle()->ItemSpacing.y + igGetFrameHeightWithSpacing();

        if (igBeginChild_Str("TerminalScrollingRegion", (ImVec2){0, -footer_height_to_reserve}, ImGuiChildFlags_None,
                             ImGuiWindowFlags_HorizontalScrollbar)) {
            igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){4, 1});
            size_t start_idx = (g_history_head + MAX_TERMINAL_HISTORY - g_history_size) % MAX_TERMINAL_HISTORY;

            for (size_t i = 0; i < g_history_size; i++) {
                size_t idx = (start_idx + i) % MAX_TERMINAL_HISTORY;

                igPushID_Int((int)i);
                igPushStyleColor_Vec4(ImGuiCol_Text, g_terminal_history[idx].color);
                
                // Stripping ANSI earlier ensures `text` is rendered without stray brackets
                // and copies perfectly cleanly to the clipboard.
                if (igSelectable_Bool(g_terminal_history[idx].text, false, ImGuiSelectableFlags_NoAutoClosePopups,
                                      (ImVec2){0, 0})) {
                    igSetClipboardText(g_terminal_history[idx].text);
                }
                igPopStyleColor(1);

                if (igIsItemHovered(ImGuiHoveredFlags_None)) igSetTooltip("Click to copy row to clipboard");
                igPopID();
            }

            if (g_scroll_to_bottom) {
                igSetScrollHereY(1.0f);
                g_scroll_to_bottom = false;
            }
            igPopStyleVar(1);
        }
        igEndChild();

        igSeparator();

        // Also after a row is clicked: the click moved focus to the suggestion
        // window, and the input has to take it back for the completion to be
        // applied and for typing to carry on.
        if (igIsWindowAppearing() || g_refocus_input) {
            igSetKeyboardFocusHere(0);
            g_refocus_input = false;
        }

        ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
                                          ImGuiInputTextFlags_CallbackCompletion |
                                          ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackAlways;
        igPushItemWidth(-1.0f);
        igPushStyleColor_Vec4(ImGuiCol_FrameBg, COLOR_BG);
        igPushStyleColor_Vec4(ImGuiCol_NavCursor, (ImVec4){0.0f, 0.0f, 0.0f, 0.0f});

        if (igInputText("##ConsoleInput", g_input_buffer, INPUT_BUFFER_SIZE, input_flags, terminal_input_callback,
                        NULL)) {
            char *trimmed = g_input_buffer;
            while (*trimmed == ' ') trimmed++;

            if (trimmed[0] != '\0') {
                terminal_push_command_history(trimmed);
                libmse_log_printf("> %s", trimmed);
                terminal_execute_command(trimmed);
            }

            g_input_buffer[0]     = '\0';
            g_command_history_pos = -1;
            g_suggestion_count    = 0;
            g_suggestion_total    = 0;
            g_suggestion_index    = 0;
            g_suggestions_dirty   = false;
            igSetKeyboardFocusHere(-1);
        }

        // Where to hang the menu, and whether the input still wants it.
        g_input_min    = igGetItemRectMin();
        g_input_max    = igGetItemRectMax();
        g_input_active = igIsItemActive();

        igPopStyleColor(2);
        igPopItemWidth();

        if (g_suggestions_dirty) {
            g_suggestions_dirty = false;
            terminal_rebuild_suggestions(g_input_buffer);
        }
    } else {
        g_input_active = false;
    }
    igEnd();

    igPopStyleColor(4);
    igPopStyleVar(3);

    // After the console is closed out, so the list is a sibling window and can
    // hang past the console's edge instead of being clipped inside it.
    terminal_draw_suggestions();
}