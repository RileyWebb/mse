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

#define MAX_TERMINAL_HISTORY 512
#define MAX_COMMAND_HISTORY 64
#define INPUT_BUFFER_SIZE 512

// Reusable Console Colors
#define COLOR_ERROR (ImVec4){0.95f, 0.35f, 0.35f, 1.0f}
#define COLOR_SUCCESS (ImVec4){0.52f, 0.95f, 0.52f, 1.0f}
#define COLOR_INFO (ImVec4){0.62f, 0.52f, 0.96f, 1.0f}
#define COLOR_ECHO (ImVec4){0.74f, 0.74f, 0.78f, 1.0f}
#define COLOR_MATCH (ImVec4){0.80f, 0.80f, 0.80f, 1.0f}
#define COLOR_TEXT (ImVec4){0.75f, 0.75f, 0.78f, 1.0f}
#define COLOR_BG (ImVec4){0.0745098039f, 0.0745098039f, 0.0745098039f, 1.0f}

typedef struct {
    FILE *stream;       // The FILE handle you write to
    char *buffer;       // Pointer to the raw allocated string block
    size_t size;        // Current length of the text inside the buffer
} MemConsole;

typedef struct {
	char   text[256];
	ImVec4 color;
} TerminalLine;

typedef struct {
	const char *word;
	int			word_len;
	int			match_count;
	const char *last_match;
	bool		print_mode;
} AutocompleteState;

static MemConsole g_mem_console = {0};

// Output display state
static TerminalLine g_terminal_history[MAX_TERMINAL_HISTORY];
static size_t		g_history_head					  = 0;
static size_t		g_history_size					  = 0;
static bool			g_scroll_to_bottom				  = false;
static char			g_input_buffer[INPUT_BUFFER_SIZE] = {0};

// Command input history state
static char	  g_command_history[MAX_COMMAND_HISTORY][INPUT_BUFFER_SIZE];
static size_t g_command_history_count				   = 0;
static int	  g_command_history_pos					   = -1;
static char	  g_command_temp_buffer[INPUT_BUFFER_SIZE] = "";

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
    if (start[0] != '\033' || (start + 1 >= end) || start[1] != '[') {
        return start + 1; // Not an ANSI escape code, advance 1 char
    }

    const char *p = start + 2; // Move past '\033['

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
            // Note: You can add param == 39 to reset to default terminal color
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

/*const char* default_color_parser(const char* start, const char* end, ImU32 *color)
{
    if (!start || !end || start >= end)
        return end;

    // 1. Look for the closing "}}" within the bounded range
    const char *closing_brackets = NULL;
    for (const char *p = start; p <= end - 2; p++) {
        if (p[0] == '}' && p[1] == '}') {
            closing_brackets = p;
            break;
        }
    }

    // 2. Parse the RGB values using sscanf
    int r = 0, g = 0, b = 0;
    int items_parsed = sscanf(start, "{{%i;%i;%i}}", &r, &g, &b);

    // 3. Validation: We must find "}}" and have parsed exactly 3 integers successfully
    if (items_parsed != 3 || !closing_brackets) {
        if (!closing_brackets) {
            return end;
        }
        return closing_brackets + 2; // Skip past the invalid formatting block
    }

    // 4. Convert and assign the color (Using ImGui's C API equivalents)
    ImColor im_color = ImColor_ImColor_Int(r, g, b, 255); 
    if (color) {
        *color = ImGui_ColorConvertFloat4ToU32(im_color.Value);
    }

    return closing_brackets + 2; // Return pointer right after "}}"
}*/

static ImVec4 mse_frontend_ui_log_level_color(DEBUG_LOG_LEVEL level)
{
	switch (level) {
	case DEBUG_LOG_LEVEL_TRACE:
		return (ImVec4){0.64f, 0.50f, 0.92f, 1.0f};
	case DEBUG_LOG_LEVEL_DEBUG:
		return (ImVec4){0.36f, 0.72f, 0.95f, 1.0f};
	case DEBUG_LOG_LEVEL_INFO:
		return (ImVec4){0.36f, 0.82f, 0.52f, 1.0f};
	case DEBUG_LOG_LEVEL_WARN:
		return (ImVec4){0.92f, 0.74f, 0.32f, 1.0f};
	case DEBUG_LOG_LEVEL_ERROR:
		return (ImVec4){0.96f, 0.40f, 0.34f, 1.0f};
	case DEBUG_LOG_LEVEL_FATAL:
		return (ImVec4){0.95f, 0.24f, 0.66f, 1.0f};
	case DEBUG_LOG_LEVEL_ASSERT:
		return (ImVec4){0.72f, 0.72f, 0.76f, 1.0f};
	default:
		return (ImVec4){0.75f, 0.75f, 0.78f, 1.0f};
	}
}

static void terminal_log(const char *text, ImVec4 color)
{
	strncpy(g_terminal_history[g_history_head].text, text, sizeof(g_terminal_history[g_history_head].text) - 1);
	g_terminal_history[g_history_head].text[sizeof(g_terminal_history[g_history_head].text) - 1] = '\0';
	g_terminal_history[g_history_head].color													 = color;

	g_history_head = (g_history_head + 1) % MAX_TERMINAL_HISTORY;
	if (g_history_size < MAX_TERMINAL_HISTORY) g_history_size++;
	g_scroll_to_bottom = true;
}

static const char *g_debug_log_strings[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "ASSRT"};

void mse_frontend_terminal_log_callback(const char *message)
{
	terminal_log(message, COLOR_TEXT);
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
	libmse_debug_register_callback(mse_frontend_terminal_log_callback);

	libmse_cmd_register(&(libmse_cmd_t){"clear", "Flushes active history array lines", 0, cmd_clear_handler});
	libmse_cmd_register(&(libmse_cmd_t){"exit", "Exits the application immediately", 0, cmd_exit_handler});
	libmse_cmd_parse("alias quit exit");
}

static void autocomplete_cvar_callback(libmse_cvar_t *cvar, void *user_data)
{
	AutocompleteState *state = (AutocompleteState *)user_data;
	if (strncmp(cvar->name, state->word, state->word_len) == 0) {
		state->match_count++;
		state->last_match = cvar->name;
		if (state->print_mode) {
			char match_str[128];
			snprintf(match_str, sizeof(match_str), "  %s", cvar->name);
			terminal_log(match_str, COLOR_MATCH);
		}
	}
}

static void autocomplete_cmd_callback(const libmse_cmd_t *cmd, void *user_data)
{
	AutocompleteState *state = (AutocompleteState *)user_data;
	if (strncmp(cmd->name, state->word, state->word_len) == 0) {
		state->match_count++;
		state->last_match = cmd->name;
		if (state->print_mode) {
			char match_str[128];
			snprintf(match_str, sizeof(match_str), "  %s (Command)", cmd->name);
			terminal_log(match_str, COLOR_MATCH);
		}
	}
}

static int terminal_input_callback(ImGuiInputTextCallbackData *data)
{
	if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
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
			data->BufDirty												= true;
		}
	} else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
		int word_start = data->BufTextLen;
		while (word_start > 0 && data->Buf[word_start - 1] != ' ') word_start--;
		int			word_len = data->BufTextLen - word_start;
		const char *word	 = data->Buf + word_start;

		if (word_len <= 0) return 0;

		AutocompleteState state = {word, word_len, 0, NULL, false};

		// Dynamic iteration captures everything perfectly
		libmse_cvar_iterate(autocomplete_cvar_callback, &state);
		libmse_cmd_iterate(autocomplete_cmd_callback, &state);

		if (state.match_count == 1) {
			if (word_start + (int)strlen(state.last_match) + 1 < data->BufSize) {
				strcpy(data->Buf + word_start, state.last_match);
				strcat(data->Buf, " ");
				data->BufTextLen = (int)strlen(data->Buf);
				data->CursorPos	 = data->BufTextLen;
				data->BufDirty	 = true;
			}
		} else if (state.match_count > 1) {
			terminal_log("Possible completions:", COLOR_INFO);
			state.print_mode = true;
			libmse_cvar_iterate(autocomplete_cvar_callback, &state);
			libmse_cmd_iterate(autocomplete_cmd_callback, &state);
		}
	}
	return 0;
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

	bool open = state->show_terminal;
	if (igBegin("MSE_CONSOLE", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse)) {

		float footer_height_to_reserve = igGetStyle()->ItemSpacing.y + igGetFrameHeightWithSpacing();

		if (igBeginChild_Str("TerminalScrollingRegion", (ImVec2){0, -footer_height_to_reserve}, ImGuiChildFlags_None,
							 ImGuiWindowFlags_HorizontalScrollbar)) {
			igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){4, 1});
			size_t start_idx = (g_history_head + MAX_TERMINAL_HISTORY - g_history_size) % MAX_TERMINAL_HISTORY;

			for (size_t i = 0; i < g_history_size; i++) {
				size_t idx = (start_idx + i) % MAX_TERMINAL_HISTORY;

				igPushID_Int((int)i);
				igPushStyleColor_Vec4(ImGuiCol_Text, g_terminal_history[idx].color);
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

		if (igIsWindowAppearing()) igSetKeyboardFocusHere(0);

		ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
										  ImGuiInputTextFlags_CallbackCompletion;
		igPushItemWidth(-1.0f);
		igPushStyleColor_Vec4(ImGuiCol_FrameBg, COLOR_BG);
		igPushStyleColor_Vec4(ImGuiCol_NavCursor, (ImVec4){0.0f, 0.0f, 0.0f, 0.0f});

		if (igInputText("##ConsoleInput", g_input_buffer, INPUT_BUFFER_SIZE, input_flags, terminal_input_callback,
						NULL)) {
			char *trimmed = g_input_buffer;
			while (*trimmed == ' ') trimmed++;

			if (trimmed[0] != '\0') {
				terminal_push_command_history(trimmed);
				libmse_debug_printf("> %s", trimmed);
				terminal_execute_command(trimmed);
			}

			g_input_buffer[0]	  = '\0';
			g_command_history_pos = -1;
			igSetKeyboardFocusHere(-1);
		}
		igPopStyleColor(2);
		igPopItemWidth();
	}
	igEnd();

	state->show_terminal = open;
	igPopStyleColor(4);
	igPopStyleVar(3);
}