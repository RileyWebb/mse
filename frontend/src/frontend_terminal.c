#define DEBUG_LOG_SOURCE "frontend"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "frontend_ui.h"
#include "frontend_imgui.h"
#include "frontend_cimgui.h"
#include "frontend_widgets.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_cmd.h"
#include "libmse/libmse_debug.h"
#include "frontend_app.h"

// Taken from the theme so the console tracks whatever palette is in force.
// These are read where a line is written, not where it is drawn, so an old
// line keeps the colour it was logged under -- which is what you want when
// scrolling back through a session.
#define COLOR_ERROR   (mse_frontend_theme()->danger)
#define COLOR_SUCCESS (mse_frontend_theme()->success)
#define COLOR_INFO    (mse_frontend_theme()->accent)
#define COLOR_ECHO    (mse_frontend_theme()->text_muted)
#define COLOR_MATCH   (mse_frontend_theme()->text)
#define COLOR_TEXT    (mse_frontend_theme()->text_muted)
#define COLOR_BG      (mse_frontend_theme()->bg_base)

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
#define SUGGESTION_USAGE_MAX  96
#define SUGGESTION_DETAIL_MAX 128
#define SUGGESTION_VISIBLE    8 // rows on screen before the list scrolls

typedef enum {
    SUGGESTION_CMD = 0,
    SUGGESTION_CVAR,
    SUGGESTION_ALIAS
} TerminalSuggestionKind;

typedef struct {
    char name[SUGGESTION_NAME_MAX];
    // For a command, the arguments it takes; for a cvar, its current value;
    // for an alias, what it expands to. The thing you need to see before you
    // press enter, in other words.
    char usage[SUGGESTION_USAGE_MAX];
    char detail[SUGGESTION_DETAIL_MAX];
    TerminalSuggestionKind kind;
} TerminalSuggestion;

static TerminalSuggestion g_suggestions[MAX_SUGGESTIONS];
static int                g_suggestion_count = 0;
static int                g_suggestion_index = 0;
static int                g_suggestion_total = 0; // matches found, may exceed the array
// How much of each name the user has already typed, so the list can show what
// completing would add rather than repeating what is already in the input.
static int                g_suggestion_prefix_len = 0;
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

// Long lines -- a path, a listvars row, an error with a file in it -- are the
// ones worth reading, and they are exactly the ones that run off the side.
// Wrapping is on for that reason; turning it off gives a horizontal scrollbar
// and one row per line, which is what you want when the output is a table.
LIBMSE_CVAR_DEFINE_INT(g_cv_terminal_wrap, "mse_terminal_wrap", 1,
                       "Wrap long console lines instead of scrolling sideways (0 = No, 1 = Yes)");

static bool cmd_clear_handler(int argc, const char** argv)
{
    g_history_head = 0;
    g_history_size = 0;
    return true;
}

// The whole buffer as one blob. Clicking a row copies that row, which is right
// for reading one value back and useless for pasting a session somewhere.
static void terminal_copy_all(void)
{
    if (g_history_size == 0) return;

    size_t       needed    = 1;
    const size_t start_idx = (g_history_head + MAX_TERMINAL_HISTORY - g_history_size) % MAX_TERMINAL_HISTORY;

    for (size_t i = 0; i < g_history_size; i++) {
        needed += strlen(g_terminal_history[(start_idx + i) % MAX_TERMINAL_HISTORY].text) + 1;
    }

    char *blob = (char *)malloc(needed);
    if (blob == NULL) return;

    size_t written = 0;
    for (size_t i = 0; i < g_history_size; i++) {
        const char  *line = g_terminal_history[(start_idx + i) % MAX_TERMINAL_HISTORY].text;
        const size_t len  = strlen(line);
        memcpy(blob + written, line, len);
        written += len;
        blob[written++] = '\n';
    }
    blob[written] = '\0';

    igSetClipboardText(blob);
    free(blob);
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
static void terminal_add_suggestion(const char *name, const char *usage, const char *detail,
                                    TerminalSuggestionKind kind)
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
    snprintf(slot->usage, sizeof(slot->usage), "%s", usage ? usage : "");
    snprintf(slot->detail, sizeof(slot->detail), "%s", detail ? detail : "");
    slot->kind = kind;

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

    // The current value stands in for a usage line: when you are completing a
    // cvar you are usually about to change it, and what it is now is the thing
    // you wanted to know.
    char usage[SUGGESTION_USAGE_MAX];
    switch (cvar->type) {
        case LIBMSE_CVAR_INT:
            snprintf(usage, sizeof(usage), "= %d", cvar->data.i ? *cvar->data.i : 0);
            break;
        case LIBMSE_CVAR_FLOAT:
            snprintf(usage, sizeof(usage), "= %.3f", cvar->data.f ? *cvar->data.f : 0.0f);
            break;
        case LIBMSE_CVAR_DOUBLE:
            snprintf(usage, sizeof(usage), "= %.3f", cvar->data.d ? *cvar->data.d : 0.0);
            break;
        case LIBMSE_CVAR_STRING:
            snprintf(usage, sizeof(usage), "= \"%s\"",
                     (cvar->data.s && *cvar->data.s) ? *cvar->data.s : "");
            break;
        default:
            usage[0] = '\0';
            break;
    }

    terminal_add_suggestion(cvar->name, usage, cvar->description, SUGGESTION_CVAR);
}

static void autocomplete_cmd_callback(const libmse_cmd_t *cmd, void *user_data)
{
    AutocompleteState *state = (AutocompleteState *)user_data;
    if (cmd->name == NULL || !terminal_prefix_match(cmd->name, state->prefix, state->prefix_len)) {
        return;
    }

    // A command that never declared its usage still shows its shape, so the
    // required argument count is not a surprise at the point of pressing enter.
    char usage[SUGGESTION_USAGE_MAX];
    if (cmd->usage != NULL && cmd->usage[0] != '\0') {
        snprintf(usage, sizeof(usage), "%s", cmd->usage);
    } else {
        usage[0] = '\0';
        size_t used = 0;
        for (size_t i = 0; i < cmd->expected_args_count && used < sizeof(usage); i++) {
            const int n = snprintf(usage + used, sizeof(usage) - used, i == 0 ? "<arg%zu>" : " <arg%zu>", i + 1);
            if (n < 0) break;
            used += (size_t)n;
        }
    }

    terminal_add_suggestion(cmd->name, usage, cmd->description, SUGGESTION_CMD);
}

static void autocomplete_alias_callback(const libmse_alias_t *alias, void *user_data)
{
    AutocompleteState *state = (AutocompleteState *)user_data;
    if (alias->name == NULL || !terminal_prefix_match(alias->name, state->prefix, state->prefix_len)) {
        return;
    }

    char usage[SUGGESTION_USAGE_MAX];
    snprintf(usage, sizeof(usage), "-> %s", alias->cmd ? alias->cmd : "");
    terminal_add_suggestion(alias->name, usage, "", SUGGESTION_ALIAS);
}

static void terminal_rebuild_suggestions(const char *buffer)
{
    g_suggestion_count = 0;
    g_suggestion_total = 0;

    int       word_start = 0;
    const int word_len   = terminal_completion_word(buffer, &word_start);
    g_suggestion_prefix_len = (word_len > 0) ? word_len : 0;
    if (word_len <= 0) {
        g_suggestion_index = 0;
        return;
    }

    AutocompleteState state = {buffer + word_start, word_len};
    libmse_cmd_iterate(autocomplete_cmd_callback, &state);
    libmse_cvar_iterate(autocomplete_cvar_callback, &state);
    libmse_alias_iterate(autocomplete_alias_callback, &state);

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
static const char *terminal_kind_label(TerminalSuggestionKind kind)
{
    switch (kind) {
        case SUGGESTION_CVAR:  return "cvar";
        case SUGGESTION_ALIAS: return "alias";
        case SUGGESTION_CMD:
        default:               return "cmd";
    }
}

static ImVec4 terminal_kind_colour(TerminalSuggestionKind kind)
{
    switch (kind) {
        case SUGGESTION_CVAR:  return mse_frontend_theme()->info;
        case SUGGESTION_ALIAS: return mse_frontend_theme()->warning;
        case SUGGESTION_CMD:
        default:               return mse_frontend_theme()->success;
    }
}

// The row SetScrollHereY was last called for. Kept so a selection that has not
// moved does not re-centre itself every frame.
static int g_suggestion_scrolled_to = -1;

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

    // The kind reads as a coloured chip rather than as a word in the middle of
    // the row: it is the one field that is the same handful of values every
    // time, so it wants to be recognised rather than read.
    const float chip_pad = mse_frontend_ui_px(5.0f);

    // Columns are measured from the entries actually on screen, so a list of
    // short cvar names does not leave a canyon before the descriptions and a
    // long usage string is not written over the one next to it.
    const float gap        = mse_frontend_ui_px(14.0f);
    const float indent     = mse_frontend_ui_px(8.0f);
    float       name_w     = 0.0f;
    float       kind_w     = 0.0f;
    float       usage_w    = 0.0f;
    float       detail_w   = 0.0f;
    for (int i = 0; i < g_suggestion_count; i++) {
        const TerminalSuggestion *sg = &g_suggestions[i];
        const float n = igCalcTextSize(sg->name, NULL, false, 0.0f).x;
        const float k = igCalcTextSize(terminal_kind_label(sg->kind), NULL, false, 0.0f).x;
        const float u = igCalcTextSize(sg->usage, NULL, false, 0.0f).x;
        const float d = igCalcTextSize(sg->detail, NULL, false, 0.0f).x;
        if (n > name_w) name_w = n;
        if (k > kind_w) kind_w = k;
        if (u > usage_w) usage_w = u;
        if (d > detail_w) detail_w = d;
    }

    const float kind_column   = indent + name_w + gap;
    const float usage_column  = kind_column + kind_w + (chip_pad * 2.0f) + gap;
    const float detail_column = usage_column + usage_w + gap;

    const float input_width = g_input_max.x - g_input_min.x;
    float       width       = detail_column + detail_w + indent + igGetStyle()->ScrollbarSize;
    if (width < input_width) {
        width = input_width;
    }
    // Never wider than the space to the right of the input, or the list hangs
    // off the edge of the window it belongs to.
    const ImGuiViewport *viewport   = igGetMainViewport();
    const float          right_edge = viewport->WorkPos.x + viewport->WorkSize.x - mse_frontend_ui_px(8.0f);
    if (g_input_min.x + width > right_edge) {
        width = right_edge - g_input_min.x;
    }

    igSetNextWindowPos((ImVec2){g_input_min.x, g_input_max.y + mse_frontend_ui_px(2.0f)}, ImGuiCond_Always,
                       (ImVec2){0.0f, 0.0f});
    igSetNextWindowSize((ImVec2){width, height}, ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;

    igPushStyleColor_Vec4(ImGuiCol_WindowBg, mse_frontend_theme()->bg_overlay);
    igPushStyleColor_Vec4(ImGuiCol_Border, mse_frontend_theme_alpha(mse_frontend_theme()->accent, 0.45f));
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, mse_frontend_ui_px(1.0f));
    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, mse_frontend_ui_px(6.0f));

    if (igBegin("##MSE_CONSOLE_SUGGESTIONS", NULL, flags)) {
        g_list_hovered = igIsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        // The list is a sibling of the console, and typing in the console
        // focuses it, which puts it in front of everything unfocused -- this
        // list included. Push the list to the front of the draw order every
        // frame instead, which leaves focus where the input needs it.
        igBringWindowToDisplayFront(igGetCurrentWindow());

        // Selectable sizes itself to the content region, which the window
        // padding insets on both sides, so the highlight stopped short of the
        // edges and read as a box rather than as a row. Measure the full width
        // once and start each row at the window's own left edge.
        const float pad_x = igGetStyle()->WindowPadding.x;
        const float row_w = igGetContentRegionAvail().x + (pad_x * 2.0f);

        for (int i = 0; i < g_suggestion_count; i++) {
            const TerminalSuggestion *suggestion = &g_suggestions[i];

            igPushID_Int(i);
            igSetCursorPosX(0.0f);
            if (igSelectable_Bool("##row", i == g_suggestion_index,
                                  ImGuiSelectableFlags_AllowOverlap, (ImVec2){row_w, 0.0f})) {
                g_suggestion_index = i;
                g_apply_completion = true;
                g_refocus_input    = true;
            }

            // Keeps the highlighted row on screen while the arrows move it.
            // Only when the selection actually moved: scrolling every frame
            // would fight the mouse wheel, and only on appearing -- which is
            // what this used to do -- meant the arrows walked the highlight
            // straight off the bottom of the list.
            if (i == g_suggestion_index && g_suggestion_index != g_suggestion_scrolled_to) {
                igSetScrollHereY(0.5f);
                g_suggestion_scrolled_to = g_suggestion_index;
            }

            const bool current = (i == g_suggestion_index);

            // The matched prefix in the accent, the rest in the kind's colour:
            // what you typed and what completing it would add, in one word.
            igSameLine(indent, 0.0f);
            if (g_suggestion_prefix_len > 0 &&
                (int)strlen(suggestion->name) >= g_suggestion_prefix_len) {
                char head[64];
                const int head_len = (g_suggestion_prefix_len < (int)sizeof(head) - 1)
                                         ? g_suggestion_prefix_len
                                         : (int)sizeof(head) - 1;
                memcpy(head, suggestion->name, (size_t)head_len);
                head[head_len] = '\0';

                igTextColored(mse_frontend_theme()->accent, "%s", head);
                igSameLine(0.0f, 0.0f);
                igTextColored(terminal_kind_colour(suggestion->kind), "%s", suggestion->name + head_len);
            } else {
                igTextColored(terminal_kind_colour(suggestion->kind), "%s", suggestion->name);
            }

            // The kind as a filled chip in its own colour.
            {
                const char  *label  = terminal_kind_label(suggestion->kind);
                const ImVec4 colour = terminal_kind_colour(suggestion->kind);

                igSameLine(kind_column, 0.0f);
                const ImVec2 at     = igGetCursorScreenPos();
                const ImVec2 extent = igCalcTextSize(label, NULL, false, 0.0f);

                ImDrawList_AddRectFilled(igGetWindowDrawList(),
                                         (ImVec2){at.x - chip_pad, at.y},
                                         (ImVec2){at.x + extent.x + chip_pad, at.y + extent.y},
                                         mse_frontend_theme_u32(colour, current ? 0.30f : 0.18f),
                                         mse_frontend_ui_px(4.0f), 0);
                igTextColored(mse_frontend_theme_alpha(colour, current ? 1.0f : 0.85f), "%s", label);
            }

            if (suggestion->usage[0] != '\0') {
                igSameLine(usage_column, 0.0f);
                igTextColored(COLOR_MATCH, "%s", suggestion->usage);
            }

            if (suggestion->detail[0] != '\0') {
                igSameLine(detail_column, 0.0f);
                igTextColored(current ? mse_frontend_theme()->text : mse_frontend_theme()->text_muted, "%s",
                              suggestion->detail);
            }
            igPopID();
        }

        if (truncated) {
            igTextColored(mse_frontend_theme()->text_faint, "  %d more, keep typing to narrow",
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
    igPushStyleColor_Vec4(ImGuiCol_Border, mse_frontend_theme()->border);
    igPushStyleColor_Vec4(ImGuiCol_TitleBg, mse_frontend_theme()->bg_raised);
    igPushStyleColor_Vec4(ImGuiCol_TitleBgActive, mse_frontend_theme()->bg_raised);

    igSetNextWindowSize((ImVec2){mse_frontend_ui_px(520.0f), mse_frontend_ui_px(360.0f)}, ImGuiCond_FirstUseEver);

    bool terminal_open = state->show_terminal != 0;
    const bool terminal_visible = igBegin("Console###MSE_CONSOLE", &terminal_open,
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
    state->show_terminal = terminal_open ? 1 : 0;

    if (terminal_visible) {

        float footer_height_to_reserve = igGetStyle()->ItemSpacing.y + igGetFrameHeightWithSpacing();

        const bool wrap = *g_cv_terminal_wrap != 0;

        // No horizontal scrollbar while wrapping: there is nothing to scroll to,
        // and leaving it on reserves room for a bar that never appears.
        const ImGuiWindowFlags scroll_flags = wrap ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar;

        if (igBeginChild_Str("TerminalScrollingRegion", (ImVec2){0, -footer_height_to_reserve}, ImGuiChildFlags_None,
                             scroll_flags)) {
            igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){4, 1});
            size_t start_idx = (g_history_head + MAX_TERMINAL_HISTORY - g_history_size) % MAX_TERMINAL_HISTORY;

            const float wrap_width = igGetContentRegionAvail().x;

            for (size_t i = 0; i < g_history_size; i++) {
                size_t      idx  = (start_idx + i) % MAX_TERMINAL_HISTORY;
                const char *text = g_terminal_history[idx].text;

                igPushID_Int((int)i);
                igPushStyleColor_Vec4(ImGuiCol_Text, g_terminal_history[idx].color);

                // Stripping ANSI earlier ensures `text` is rendered without stray
                // brackets and copies perfectly cleanly to the clipboard.
                bool clicked;
                if (wrap) {
                    // A selectable cannot wrap its own label, so it is drawn as a
                    // blank one tall enough for the wrapped text and the text is
                    // written back over it. Measuring first is what keeps the row
                    // and its highlight the same height.
                    const ImVec2 extent = igCalcTextSize(text, NULL, false, wrap_width);

                    const ImVec2 origin = igGetCursorScreenPos();
                    clicked = igSelectable_Bool("##row", false, ImGuiSelectableFlags_NoAutoClosePopups,
                                                (ImVec2){0, extent.y});
                    const bool hovered = igIsItemHovered(ImGuiHoveredFlags_None);

                    igSetCursorScreenPos(origin);
                    igPushTextWrapPos(igGetCursorPosX() + wrap_width);
                    igTextUnformatted(text, NULL);
                    igPopTextWrapPos();

                    if (hovered) igSetTooltip("Click to copy row to clipboard");
                } else {
                    clicked = igSelectable_Bool(text, false, ImGuiSelectableFlags_NoAutoClosePopups, (ImVec2){0, 0});
                    if (igIsItemHovered(ImGuiHoveredFlags_None)) igSetTooltip("Click to copy row to clipboard");
                }

                if (clicked) igSetClipboardText(text);

                igPopStyleColor(1);
                igPopID();
            }

            if (g_scroll_to_bottom) {
                igSetScrollHereY(1.0f);
                g_scroll_to_bottom = false;
            }
            igPopStyleVar(1);

            // Right-click rather than a control in the footer: the footer is the
            // input, and shrinking that to make room for a checkbox would cost
            // more than the checkbox is worth.
            if (igBeginPopupContextWindow("##ConsoleMenu", ImGuiPopupFlags_MouseButtonRight)) {
                bool wrap_toggle = *g_cv_terminal_wrap != 0;
                if (igMenuItem_BoolPtr("Wrap lines", NULL, &wrap_toggle, true)) {
                    libmse_cvar_set_i("mse_terminal_wrap", wrap_toggle ? 1 : 0);
                }
                igSeparator();
                if (igMenuItem_Bool("Copy all", NULL, false, g_history_size > 0)) terminal_copy_all();
                if (igMenuItem_Bool("Clear", NULL, false, g_history_size > 0)) cmd_clear_handler(0, NULL);
                igEndPopup();
            }
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