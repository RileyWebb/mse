#include "libmse/debug.h"
#include "frontend_cimgui.h"
#include "frontend_imgui.h"

#include "frontend_ui.h"

#define MSE_FRONTEND_UI_LOG_CAPACITY 256

typedef struct mse_frontend_ui_log_entry_s {
	debug_log log;
} mse_frontend_ui_log_entry_t;

static mse_frontend_ui_log_entry_t g_frontend_logs[MSE_FRONTEND_UI_LOG_CAPACITY];
static size_t					   g_frontend_log_count				  = 0;
static size_t					   g_frontend_log_head				  = 0;
static bool						   g_frontend_log_callback_registered = false;

static const char *mse_frontend_ui_log_level_label(DEBUG_LOG_LEVEL level)
{
	switch (level) {
	case DEBUG_LOG_LEVEL_TRACE:
		return "TRACE";
	case DEBUG_LOG_LEVEL_DEBUG:
		return "DEBUG";
	case DEBUG_LOG_LEVEL_INFO:
		return "INFO";
	case DEBUG_LOG_LEVEL_WARN:
		return "WARN";
	case DEBUG_LOG_LEVEL_ERROR:
		return "ERROR";
	case DEBUG_LOG_LEVEL_FATAL:
		return "FATAL";
	case DEBUG_LOG_LEVEL_ASSERT:
		return "ASSERT";
	default:
		return "LOG";
	}
}

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

static void mse_frontend_ui_log_time_string(const debug_log *log, char *buffer, size_t buffer_size)
{
	if (log == NULL || buffer == NULL || buffer_size == 0) {
		return;
	}

	time_t	  curtime = log->tp.tv_sec;
	struct tm local_time;
#ifdef _WIN32
	localtime_s(&local_time, &curtime);
#else
	localtime_r(&curtime, &local_time);
#endif
	snprintf(buffer, buffer_size, "%02d:%02d:%02d.%06ld", local_time.tm_hour, local_time.tm_min, local_time.tm_sec,
			 log->tp.tv_usec);
}

void mse_frontend_ui_capture_log(const debug_log *log, void *user_data)
{
	(void)user_data;

	if (log == NULL || MSE_FRONTEND_UI_LOG_CAPACITY == 0) {
		return;
	}

	g_frontend_logs[g_frontend_log_head].log = *log;
	g_frontend_log_head						 = (g_frontend_log_head + 1) % MSE_FRONTEND_UI_LOG_CAPACITY;
	if (g_frontend_log_count < MSE_FRONTEND_UI_LOG_CAPACITY) {
		++g_frontend_log_count;
	}
}

void mse_frontend_ui_clear_logs(void)
{
	g_frontend_log_count = 0;
	g_frontend_log_head	 = 0;
}

static size_t mse_frontend_ui_log_index(size_t offset_from_oldest)
{
	if (g_frontend_log_count == 0) {
		return 0;
	}

	return (g_frontend_log_head + MSE_FRONTEND_UI_LOG_CAPACITY - g_frontend_log_count + offset_from_oldest) %
		   MSE_FRONTEND_UI_LOG_CAPACITY;
}

void mse_frontend_ui_draw_logs_view(void)
{
	igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
	igTextColored((ImVec4){0.639f, 0.443f, 0.969f, 1.0f}, "LOGS");
	igPopFont();
	igTextDisabled("Live debug stream from the frontend logger.");
	igSpacing();

	if (igButton("Clear Log", (ImVec2){110.0f, 0.0f})) {
		mse_frontend_ui_clear_logs();
	}
	igSameLine(0.0f, 10.0f);
	igTextDisabled("Entries: %zu", g_frontend_log_count);

	igSeparator();

	if (igBeginChild_Str("LOGS_SCROLL", (ImVec2){0.0f, 0.0f}, ImGuiChildFlags_Borders,
						 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		const ImGuiTableFlags table_flags =
			ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
		if (igBeginTable("logs_table", 5, table_flags, (ImVec2){0.0f, 0.0f}, 0.0f)) {
			igTableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed,
							   igCalcTextSize("00:00:00.000000", NULL, false, 0.0f).x, 0);
			igTableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 80.0f, 0);
			igTableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed, 180.0f, 0);
			igTableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
			igTableHeadersRow();

			for (size_t i = 0; i < g_frontend_log_count; ++i) {
				const mse_frontend_ui_log_entry_t *entry = &g_frontend_logs[mse_frontend_ui_log_index(i)];
				char							   time_buffer[32];
				mse_frontend_ui_log_time_string(&entry->log, time_buffer, sizeof(time_buffer));

				igTableNextRow(0, 0);

				igTableSetColumnIndex(0);
				igTextDisabled("%s", time_buffer);

				//igTableSetColumnIndex(1);
				//igTextDisabled("%s", entry->log.origin != NULL ? entry->log.origin : "backend");

				igTableSetColumnIndex(1);
				igTextColored(mse_frontend_ui_log_level_color(entry->log.level), "%s",
							  mse_frontend_ui_log_level_label(entry->log.level));

				igTableSetColumnIndex(2);
				igTextDisabled("%s:%d", entry->log.file, entry->log.line);

				igTableSetColumnIndex(3);
				igTextWrapped("%s", entry->log.message);
			}

			igEndTable();
		}
	}

	igEndChild();
}