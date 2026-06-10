#define DEBUG_LOG_SOURCE "frontend"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "frontend_ui.h"
#include "frontend_imgui.h"
#include "frontend_cimgui.h"
#include "libmse/libmse_cvar.h"
#include "libmse/debug.h"

#define MAX_TERMINAL_HISTORY 512
#define MAX_COMMAND_HISTORY 64
#define INPUT_BUFFER_SIZE 512

// Reusable Console Colors
#define COLOR_ERROR (ImVec4){0.95f, 0.35f, 0.35f, 1.0f}
#define COLOR_SUCCESS (ImVec4){0.52f, 0.95f, 0.52f, 1.0f}
#define COLOR_INFO (ImVec4){0.62f, 0.52f, 0.96f, 1.0f}
#define COLOR_ECHO (ImVec4){0.74f, 0.74f, 0.78f, 1.0f}
#define COLOR_MATCH (ImVec4){0.80f, 0.80f, 0.80f, 1.0f}
#define COLOR_BG (ImVec4){0.0745098039f, 0.0745098039f, 0.0745098039f, 1.0f}

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

// Output display state 
static TerminalLine g_terminal_history[MAX_TERMINAL_HISTORY];
static size_t       g_history_head                    = 0; 
static size_t       g_history_size                    = 0; 
static bool			g_scroll_to_bottom				  = false;
static char			g_input_buffer[INPUT_BUFFER_SIZE] = {0};

// Command input history state 
static char	  g_command_history[MAX_COMMAND_HISTORY][INPUT_BUFFER_SIZE];
static size_t g_command_history_count				   = 0;
static int	  g_command_history_pos					   = -1; 
static char	  g_command_temp_buffer[INPUT_BUFFER_SIZE] = ""; 

static ImVec4 mse_frontend_ui_log_level_color(DEBUG_LOG_LEVEL level) {
	switch (level) {
	case DEBUG_LOG_LEVEL_TRACE:  return (ImVec4){0.64f, 0.50f, 0.92f, 1.0f};
	case DEBUG_LOG_LEVEL_DEBUG:  return (ImVec4){0.36f, 0.72f, 0.95f, 1.0f};
	case DEBUG_LOG_LEVEL_INFO:   return (ImVec4){0.36f, 0.82f, 0.52f, 1.0f};
	case DEBUG_LOG_LEVEL_WARN:   return (ImVec4){0.92f, 0.74f, 0.32f, 1.0f};
	case DEBUG_LOG_LEVEL_ERROR:  return (ImVec4){0.96f, 0.40f, 0.34f, 1.0f};
	case DEBUG_LOG_LEVEL_FATAL:  return (ImVec4){0.95f, 0.24f, 0.66f, 1.0f};
	case DEBUG_LOG_LEVEL_ASSERT: return (ImVec4){0.72f, 0.72f, 0.76f, 1.0f};
	default:                     return (ImVec4){0.75f, 0.75f, 0.78f, 1.0f};
	}
}

static void terminal_log(const char *text, ImVec4 color) {
	strncpy(g_terminal_history[g_history_head].text, text, sizeof(g_terminal_history[g_history_head].text) - 1);
	g_terminal_history[g_history_head].text[sizeof(g_terminal_history[g_history_head].text) - 1] = '\0';
	g_terminal_history[g_history_head].color = color;

	g_history_head = (g_history_head + 1) % MAX_TERMINAL_HISTORY;
	if (g_history_size < MAX_TERMINAL_HISTORY) g_history_size++;
	g_scroll_to_bottom = true;
}

static const char *debug_log_strings[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "ASSRT" };

void mse_frontend_terminal_log_callback(const debug_log *log, void *user_data) {
	if (!log) return;
	char output[512], time_buffer[32];
	time_t curtime = log->tp.tv_sec;
	struct tm local_time;
#ifdef _WIN32
	localtime_s(&local_time, &curtime);
#else
	localtime_r(&curtime, &local_time);
#endif
	snprintf(time_buffer, sizeof(time_buffer), "%02d:%02d:%02d.%06ld", local_time.tm_hour, local_time.tm_min, local_time.tm_sec, log->tp.tv_usec);
	snprintf(output, sizeof(output), "[%s] %s: %s", time_buffer, debug_log_strings[log->level], log->message);
	terminal_log(output, mse_frontend_ui_log_level_color(log->level));
}

static void terminal_push_command_history(const char *cmd) {
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

// ---------------------------------------------------------
// DYNAMIC COMMAND HANDLERS
// ---------------------------------------------------------

static void help_cmd_callback(const libmse_command_t *cmd, void *user_data) {
	char output[256];
	snprintf(output, sizeof(output), "  %-30s - %s", cmd->name, cmd->description ? cmd->description : "");
	terminal_log(output, COLOR_INFO);
}

static bool cmd_help_handler(const libmse_cmd_arg_t* args) {
	terminal_log("", COLOR_INFO);
	terminal_log("Available Commands:", COLOR_INFO);
	libmse_command_iterate(help_cmd_callback, NULL);
	terminal_log("", COLOR_INFO);
	return true;
}

static bool cmd_clear_handler(const libmse_cmd_arg_t* args) {
	g_history_head = 0;
	g_history_size = 0;
	return true;
}

static void list_cvar_callback(libmse_cvar_t *cvar, void *user_data) {
	char output[256], val_str[64] = "";
	const char *type_str = "Unknown";

	if (cvar->type == LIBMSE_CVAR_INT) { type_str = "Int"; snprintf(val_str, sizeof(val_str), "%d", *cvar->data.i); }
	else if (cvar->type == LIBMSE_CVAR_DOUBLE) { type_str = "Double"; snprintf(val_str, sizeof(val_str), "%.4f", *cvar->data.d); }
	else if (cvar->type == LIBMSE_CVAR_FLOAT) { type_str = "Float"; snprintf(val_str, sizeof(val_str), "%.4f", *cvar->data.f); }
	else if (cvar->type == LIBMSE_CVAR_STRING) { type_str = "String"; snprintf(val_str, sizeof(val_str), "\"%s\"", *cvar->data.s ? *cvar->data.s : "NULL"); }

	snprintf(output, sizeof(output), "  %-30s = %-10s [%-6s] : %s", cvar->name, val_str, type_str, cvar->description ? cvar->description : "");
	terminal_log(output, (ImVec4){0.85f, 0.85f, 0.85f, 1.0f});
}

static void list_cmd_callback(const libmse_command_t *cmd, void *user_data) {
	char output[256];
	snprintf(output, sizeof(output), "  %-30s (", cmd->name);
	for (size_t i = 0; i < cmd->expected_args_count; i++)
	{
		libmse_cvar_type_t type = cmd->expected_types[i];
		const char* type_str = "Unknown";
		if (type == LIBMSE_CVAR_INT) { type_str = "Int"; }
		else if (type == LIBMSE_CVAR_DOUBLE) { type_str = "Double"; }
		else if (type == LIBMSE_CVAR_FLOAT) { type_str = "Float"; }
		else if (type == LIBMSE_CVAR_STRING) { type_str = "String"; }
		strncat(output, type_str, sizeof(output) - strlen(output) - 1);
		if (i < cmd->expected_args_count - 1) strncat(output, ", ", sizeof(output) - strlen(output) - 1);
	}
	
	strncat(output, ") - ", sizeof(output) - strlen(output) - 1);
	strncat(output, cmd->description ? cmd->description : "", sizeof(output) - strlen(output) - 1);

	terminal_log(output, (ImVec4){0.85f, 0.85f, 0.95f, 1.0f});
}

static bool cmd_list_handler(const libmse_cmd_arg_t* args) {
	terminal_log("Registered CVars:", COLOR_INFO);
	libmse_cvar_iterate(list_cvar_callback, NULL);
	terminal_log("--- End of List ---", COLOR_ECHO);
	terminal_log("", COLOR_INFO);
	terminal_log("Registered Commands:", COLOR_INFO);
	libmse_command_iterate(list_cmd_callback, NULL);
	terminal_log("--- End of List ---", COLOR_ECHO);
	return true;
}

static bool cmd_exit_handler(const libmse_cmd_arg_t* args) {
	exit(0);
	return true;
}

static bool cmd_print_handler(const libmse_cmd_arg_t* args) {
	terminal_log(args[0].s, COLOR_INFO);
	return true;
}

static bool cmd_get_handler(const libmse_cmd_arg_t* args) {
	const char* name = args[0].s;
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar) {
		terminal_log("Error: CVar identifier target not found.", COLOR_ERROR);
		return false;
	}

	char output[256];
	switch (cvar->type) {
		case LIBMSE_CVAR_INT: snprintf(output, sizeof(output), "%s = %d (Int)", cvar->name, *cvar->data.i); break;
		case LIBMSE_CVAR_FLOAT: snprintf(output, sizeof(output), "%s = %.4f (Float)", cvar->name, *cvar->data.f); break;
		case LIBMSE_CVAR_STRING: snprintf(output, sizeof(output), "%s = \"%s\" (String)", cvar->name, *cvar->data.s ? *cvar->data.s : "NULL"); break;
	}
	terminal_log(output, COLOR_SUCCESS);
	return true;
}

static bool cmd_set_handler(const libmse_cmd_arg_t* args) {
	const char* name = args[0].s;
	const char* val_str = args[1].s;

	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar) {
		terminal_log("Error: CVar identifier target not found.", COLOR_ERROR);
		return false;
	}

	bool sync_success = false;
	if (cvar->type == LIBMSE_CVAR_INT) sync_success = libmse_cvar_set_i(name, atoi(val_str));
	else if (cvar->type == LIBMSE_CVAR_FLOAT) sync_success = libmse_cvar_set_f(name, (float)atof(val_str));
	else if (cvar->type == LIBMSE_CVAR_STRING) sync_success = libmse_cvar_set_s(name, val_str);

	if (sync_success) {
		char output[256];
		if (cvar->type == LIBMSE_CVAR_INT) snprintf(output, sizeof(output), "Set '%s' to %d.", name, *cvar->data.i);
		else if (cvar->type == LIBMSE_CVAR_FLOAT) snprintf(output, sizeof(output), "Set '%s' to %.4f.", name, *cvar->data.f);
		else if (cvar->type == LIBMSE_CVAR_STRING) snprintf(output, sizeof(output), "Set '%s' to \"%s\".", name, cvar->data.s ? *cvar->data.s : "NULL");
		terminal_log(output, COLOR_SUCCESS);
	} else {
		terminal_log("Error: Failed to process assignment.", COLOR_ERROR);
	}
	return true;
}

static bool cmd_create_handler(const libmse_cmd_arg_t* args) {
	const char* type_str = args[0].s;
	const char* name = args[1].s;
	const char* val_str = args[2].s;

	// Heap allocate backing variables so they aren't destroyed when this stack ends
	void* ref = NULL;
	libmse_cvar_type_t type;

	if (strcmp(type_str, "int") == 0) {
		type = LIBMSE_CVAR_INT;
		int* val = malloc(sizeof(int));
		*val = atoi(val_str);
		ref = val;
	} else if (strcmp(type_str, "float") == 0) {
		type = LIBMSE_CVAR_FLOAT;
		float* val = malloc(sizeof(float));
		*val = (float)atof(val_str);
		ref = val;
	} else if (strcmp(type_str, "string") == 0) {
		type = LIBMSE_CVAR_STRING;
		const char** val = malloc(sizeof(const char*));
		*val = strdup(val_str); // Needs to exist persistently 
		ref = val;
	} else {
		terminal_log("Error: Invalid type. Use 'int', 'float', or 'string'.", COLOR_ERROR);
		return false;
	}

	if (libmse_cvar_register(name, type, ref, "Dynamically created via console")) {
		char output[256];
		snprintf(output, sizeof(output), "Created CVar '%s' successfully.", name);
		terminal_log(output, COLOR_SUCCESS);
		return true;
	} else {
		terminal_log("Error: Failed to register CVar (Already exists?).", COLOR_ERROR);
		free(ref);
		return false;
	}
}

static bool cmd_delete_handler(const libmse_cmd_arg_t* args) {
	const char* name = args[0].s;
	if (libmse_cvar_destroy(name)) {
		char output[256];
		snprintf(output, sizeof(output), "Deleted CVar '%s' successfully.", name);
		terminal_log(output, COLOR_SUCCESS);
		return true;
	} else {
		terminal_log("Error: Failed to destroy CVar (Not found?).", COLOR_ERROR);
		return false;
	}
}

// You should call this once on frontend initialization!
void mse_frontend_terminal_init(void) {
	static const libmse_cvar_type_t arg_string_1[] = { LIBMSE_CVAR_STRING };
	static const libmse_cvar_type_t arg_string_2[] = { LIBMSE_CVAR_STRING, LIBMSE_CVAR_STRING };
	static const libmse_cvar_type_t arg_string_3[] = { LIBMSE_CVAR_STRING, LIBMSE_CVAR_STRING, LIBMSE_CVAR_STRING };

	libmse_command_register(&(libmse_command_t){ "help", "Lists all available commands", 0, NULL, cmd_help_handler });
	libmse_command_register(&(libmse_command_t){ "clear", "Flushes active history array lines", 0, NULL, cmd_clear_handler });
	libmse_command_register(&(libmse_command_t){ "list", "Lists all registered CVars and Commands", 0, NULL, cmd_list_handler });
	
	libmse_command_register(&(libmse_command_t){ "print", "Prints the specified string", 1, arg_string_1, cmd_print_handler });
	libmse_command_register(&(libmse_command_t){ "get", "Reads current CVar variant", 1, arg_string_1, cmd_get_handler });
	libmse_command_register(&(libmse_command_t){ "delete", "Destroys a target CVar", 1, arg_string_1, cmd_delete_handler });
	
	libmse_command_register(&(libmse_command_t){ "set", "Modifies target CVar allocation", 2, arg_string_2, cmd_set_handler });
	libmse_command_register(&(libmse_command_t){ "create", "Registers a new CVar", 3, arg_string_3, cmd_create_handler });

	libmse_command_register(&(libmse_command_t){ "exit", "Exits the application immediately", 0, NULL, cmd_exit_handler });
	libmse_command_register(&(libmse_command_t){ "quit", "Quits the application immediately", 0, NULL, cmd_exit_handler });
}

// ---------------------------------------------------------
// TERMINAL LOGIC
// ---------------------------------------------------------

static void autocomplete_cvar_callback(libmse_cvar_t *cvar, void *user_data) {
	AutocompleteState *state = (AutocompleteState *)user_data;
	if (strncmp(cvar->name, state->word, state->word_len) == 0) {
		state->match_count++;
		state->last_match = cvar->name;
		if (state->print_mode) {
			char match_str[128]; snprintf(match_str, sizeof(match_str), "  %s", cvar->name);
			terminal_log(match_str, COLOR_MATCH);
		}
	}
}

static void autocomplete_cmd_callback(const libmse_command_t *cmd, void *user_data) {
	AutocompleteState *state = (AutocompleteState *)user_data;
	if (strncmp(cmd->name, state->word, state->word_len) == 0) {
		state->match_count++;
		state->last_match = cmd->name;
		if (state->print_mode) {
			char match_str[128]; snprintf(match_str, sizeof(match_str), "  %s (Command)", cmd->name);
			terminal_log(match_str, COLOR_MATCH);
		}
	}
}

static int terminal_input_callback(ImGuiInputTextCallbackData *data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
		int prev_pos = g_command_history_pos;

		if (data->EventKey == ImGuiKey_UpArrow) {
			if (g_command_history_pos == -1 && g_command_history_count > 0) {
				strncpy(g_command_temp_buffer, data->Buf, INPUT_BUFFER_SIZE - 1);
				g_command_history_pos = (int)g_command_history_count - 1;
			} else if (g_command_history_pos > 0) g_command_history_pos--;
		} else if (data->EventKey == ImGuiKey_DownArrow) {
			if (g_command_history_pos != -1) {
				if ((size_t)g_command_history_pos < g_command_history_count - 1) g_command_history_pos++;
				else g_command_history_pos = -1;
			}
		}

		if (prev_pos != g_command_history_pos) {
			const char *target_str = (g_command_history_pos == -1) ? g_command_temp_buffer : g_command_history[g_command_history_pos];
			snprintf(data->Buf, (size_t)data->BufSize, "%s", target_str);
			data->BufTextLen = (int)strlen(data->Buf);
			data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
			data->BufDirty = true;
		}
	}
	else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
		int word_start = data->BufTextLen;
		while (word_start > 0 && data->Buf[word_start - 1] != ' ') word_start--;
		int word_len = data->BufTextLen - word_start;
		const char *word = data->Buf + word_start;

		if (word_len <= 0) return 0;

		AutocompleteState state = { word, word_len, 0, NULL, false };

		// Dynamic iteration captures everything perfectly
		libmse_cvar_iterate(autocomplete_cvar_callback, &state);
		libmse_command_iterate(autocomplete_cmd_callback, &state);

		if (state.match_count == 1) {
			if (word_start + (int)strlen(state.last_match) + 1 < data->BufSize) {
				strcpy(data->Buf + word_start, state.last_match);
				strcat(data->Buf, " ");
				data->BufTextLen = (int)strlen(data->Buf);
				data->CursorPos  = data->BufTextLen;
				data->BufDirty   = true;
			}
		} else if (state.match_count > 1) {
			terminal_log("Possible completions:", COLOR_INFO);
			state.print_mode = true;
			libmse_cvar_iterate(autocomplete_cvar_callback, &state);
			libmse_command_iterate(autocomplete_cmd_callback, &state);
		}
	}
	return 0;
}

static void terminal_execute_command(const char *cmd_line) {
	char line_copy[INPUT_BUFFER_SIZE];
	strncpy(line_copy, cmd_line, sizeof(line_copy) - 1);
	line_copy[sizeof(line_copy) - 1] = '\0';

	char echo[INPUT_BUFFER_SIZE + 4];
	snprintf(echo, sizeof(echo), "> %s", cmd_line);
	terminal_log(echo, COLOR_ECHO);

	char* args[16];
	int argc = 0;
	char* p = line_copy;
	
	// Robust Tokenization (Supports "Spaces inside Quotes")
	while (*p && argc < 16) {
		while (*p == ' ') p++;
		if (!*p) break;
		if (*p == '"') {
			p++; args[argc++] = p;
			while (*p && *p != '"') p++;
			if (*p == '"') { *p = '\0'; p++; }
		} else {
			args[argc++] = p;
			while (*p && *p != ' ') p++;
			if (*p == ' ') { *p = '\0'; p++; }
		}
	}

	if (argc == 0) return;
	const char* cmd = args[0];

	// 1. Check if input maps to a Registered Command
	libmse_command_t* cmd_def = libmse_command_get(cmd);
	if (cmd_def) {
		if (!libmse_command_execute(cmd, argc - 1, (const char**)&args[1])) {
			char err[128];
			snprintf(err, sizeof(err), "Error: Command '%s' failed or expected %zu arguments.", cmd, cmd_def->expected_args_count);
			terminal_log(err, COLOR_ERROR);
		}
		return;
	}

	// 2. Fallback check: Direct implicit CVar Get/Set (e.g. typing 'cl_fov 90')
	libmse_cvar_t* cvar = libmse_cvar_get(cmd);
	if (cvar) {
		if (argc == 1) { // Get logic
			cmd_get_handler(&(libmse_cmd_arg_t){.s = cmd});
		} else if (argc == 2) { // Set logic
			libmse_cmd_arg_t set_args[2] = {{.s = cmd}, {.s = args[1]}};
			cmd_set_handler(set_args);
		} else {
			terminal_log("Usage: <cvar_name> [new_value]", COLOR_ERROR);
		}
		return;
	}

	terminal_log("Unknown command or CVar. Enter 'help' for instructions.", COLOR_ERROR);
}

// Main UI Rendering Block
void mse_frontend_ui_draw_terminal(mse_frontend_ui_state_t *state) {
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

		if (igBeginChild_Str("TerminalScrollingRegion", (ImVec2){0, -footer_height_to_reserve}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
			igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){4, 1});
			size_t start_idx = (g_history_head + MAX_TERMINAL_HISTORY - g_history_size) % MAX_TERMINAL_HISTORY;

			for (size_t i = 0; i < g_history_size; i++) {
				size_t idx = (start_idx + i) % MAX_TERMINAL_HISTORY;
				
				igPushID_Int((int)i);
				igPushStyleColor_Vec4(ImGuiCol_Text, g_terminal_history[idx].color);
				if (igSelectable_Bool(g_terminal_history[idx].text, false, ImGuiSelectableFlags_NoAutoClosePopups, (ImVec2){0, 0})) {
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

		if (igIsWindowAppearing())
			igSetKeyboardFocusHere(0);

		ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;
		igPushItemWidth(-1.0f);
		igPushStyleColor_Vec4(ImGuiCol_FrameBg, COLOR_BG);
		igPushStyleColor_Vec4(ImGuiCol_NavCursor, (ImVec4){0.0f, 0.0f, 0.0f, 0.0f});

		if (igInputText("##ConsoleInput", g_input_buffer, INPUT_BUFFER_SIZE, input_flags, terminal_input_callback, NULL)) {
			char *trimmed = g_input_buffer;
			while (*trimmed == ' ') trimmed++;

			if (trimmed[0] != '\0') {
				terminal_push_command_history(trimmed);
				terminal_execute_command(trimmed);
			}

			g_input_buffer[0] = '\0';
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