// cimgui_markdown.h
#ifndef CIMGUI_MARKDOWN_H
#define CIMGUI_MARKDOWN_H

// THIS IS A MODIFIED VERSION OF THE ORIGINAL IMGUI_MARKDOWN.H
// Original source:

// License: zlib
// Copyright (c) 2019 Juliette Foucaut & Doug Binks
// Adapted for CImGui by [Your Name/Handle]
// (Original license notice from imgui_markdown.h remains)
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#include "cimgui.h" // Main CImGui header
#include <stdint.h>
#include <stdbool.h> // For bool type
#include <stddef.h>	 // For size_t

#ifdef __cplusplus
extern "C" {
#endif

//-----------------------------------------------------------------------------
// Basic types
//-----------------------------------------------------------------------------

// Forward declare structs
struct ImGuiMarkdown_Config;

typedef struct ImGuiMarkdown_LinkCallbackData {
	const char *text;
	int			textLength;
	const char *link;
	int			linkLength;
	void	   *userData;
	bool		isImage;
} ImGuiMarkdown_LinkCallbackData;

typedef struct ImGuiMarkdown_TooltipCallbackData {
	ImGuiMarkdown_LinkCallbackData linkData;
	const char					  *linkIcon;
} ImGuiMarkdown_TooltipCallbackData;

typedef struct ImGuiMarkdown_ImageData {
	bool		   isValid;
	bool		   useLinkCallback;
	ImTextureRef_c user_texture_id;
	ImVec2		   size;
	ImVec2		   uv0;
	ImVec2		   uv1;
	ImVec4		   tint_col;
	ImVec4		   border_col;
	ImVec4		   bg_col; // Added to ensure struct matches, might need IMGUI_VERSION_NUM check
} ImGuiMarkdown_ImageData;

// Helper to initialize ImGuiMarkdown_ImageData to default values
CIMGUI_API void ImGuiMarkdown_ImageData_Init(ImGuiMarkdown_ImageData *data);

typedef enum ImGuiMarkdown_FormatType {
	ImGuiMarkdown_FormatType_NORMAL_TEXT,
	ImGuiMarkdown_FormatType_HEADING,
	ImGuiMarkdown_FormatType_UNORDERED_LIST,
	ImGuiMarkdown_FormatType_LINK,
	ImGuiMarkdown_FormatType_EMPHASIS,
} ImGuiMarkdown_FormatType;

typedef struct ImGuiMarkdown_FormatInfo {
	ImGuiMarkdown_FormatType		   type;
	int32_t							   level;
	bool							   itemHovered;
	const struct ImGuiMarkdown_Config *config;
} ImGuiMarkdown_FormatInfo;

typedef void (*ImGuiMarkdown_LinkCallback)(ImGuiMarkdown_LinkCallbackData data);
typedef void (*ImGuiMarkdown_TooltipCallback)(ImGuiMarkdown_TooltipCallbackData data);
typedef ImGuiMarkdown_ImageData (*ImGuiMarkdown_ImageCallback)(ImGuiMarkdown_LinkCallbackData data);
typedef void (*ImGuiMarkdown_FormatCallback)(const ImGuiMarkdown_FormatInfo *markdownFormatInfo, bool start);

CIMGUI_API void ImGuiMarkdown_DefaultTooltipCallback(ImGuiMarkdown_TooltipCallbackData data);
CIMGUI_API void ImGuiMarkdown_DefaultFormatCallback(const ImGuiMarkdown_FormatInfo *markdownFormatInfo, bool start);

typedef struct ImGuiMarkdown_HeadingFormat {
	ImFont *font;
	bool	separator;
} ImGuiMarkdown_HeadingFormat;

#define IMGUI_MARKDOWN_NUM_HEADINGS 3

typedef struct ImGuiMarkdown_Config {
	ImGuiMarkdown_LinkCallback	  linkCallback;
	ImGuiMarkdown_TooltipCallback tooltipCallback;
	ImGuiMarkdown_ImageCallback	  imageCallback;
	const char					 *linkIcon;
	ImGuiMarkdown_HeadingFormat	  headingFormats[IMGUI_MARKDOWN_NUM_HEADINGS];
	void						 *userData;
	ImGuiMarkdown_FormatCallback  formatCallback;
} ImGuiMarkdown_Config;

// Helper to initialize ImGuiMarkdown_Config to default values
CIMGUI_API void ImGuiMarkdown_Config_Init(ImGuiMarkdown_Config *config);

//-----------------------------------------------------------------------------
// External interface
//-----------------------------------------------------------------------------

CIMGUI_API void ImGuiMarkdown(const char *markdown, size_t markdownLength, const ImGuiMarkdown_Config *mdConfig);

//-----------------------------------------------------------------------------
// Internals (exposed for potential advanced use or if needed by inline functions)
// These are typically not called directly by the user.
//-----------------------------------------------------------------------------
typedef struct ImGuiMarkdown_TextRegion ImGuiMarkdown_TextRegion; // Forward declare
typedef struct ImGuiMarkdown_Line		ImGuiMarkdown_Line;		  // Forward declare
typedef struct ImGuiMarkdown_Link		ImGuiMarkdown_Link;		  // Forward declare

CIMGUI_API void ImGuiMarkdown_UnderLine(ImU32 col); // Changed ImColor to ImU32 for CImGui
CIMGUI_API void ImGuiMarkdown_RenderLine(const char *markdown, ImGuiMarkdown_Line *line,
										 ImGuiMarkdown_TextRegion *textRegion, const ImGuiMarkdown_Config *mdConfig);

typedef struct ImGuiMarkdown_TextRegion {
	float indentX;
	// Other members if they were part of the original C++ struct's state,
	// but it seems indentX is the only state.
} ImGuiMarkdown_TextRegion;

CIMGUI_API void ImGuiMarkdown_TextRegion_Init(ImGuiMarkdown_TextRegion *region);
CIMGUI_API void ImGuiMarkdown_TextRegion_RenderTextWrapped(ImGuiMarkdown_TextRegion *region, const char *text,
														   const char *text_end, bool bIndentToHere);
CIMGUI_API void ImGuiMarkdown_TextRegion_RenderListTextWrapped(ImGuiMarkdown_TextRegion *region, const char *text,
															   const char *text_end);
CIMGUI_API bool ImGuiMarkdown_TextRegion_RenderLinkText(ImGuiMarkdown_TextRegion *region, const char *text,
														const char *text_end, const ImGuiMarkdown_Link *link,
														const char *markdown, const ImGuiMarkdown_Config *mdConfig,
														const char **linkHoverStart);
CIMGUI_API void ImGuiMarkdown_TextRegion_RenderLinkTextWrapped(ImGuiMarkdown_TextRegion *region, const char *text,
															   const char *text_end, const ImGuiMarkdown_Link *link,
															   const char				  *markdown,
															   const ImGuiMarkdown_Config *mdConfig,
															   const char **linkHoverStart, bool bIndentToHere);
CIMGUI_API void ImGuiMarkdown_TextRegion_ResetIndent(ImGuiMarkdown_TextRegion *region);

typedef struct ImGuiMarkdown_Line {
	bool isHeading;
	bool isEmphasis;
	bool isUnorderedListStart;
	bool isLeadingSpace;
	int	 leadSpaceCount;
	int	 headingCount;
	int	 emphasisCount;
	int	 lineStart;
	int	 lineEnd;
	int	 lastRenderPosition;
} ImGuiMarkdown_Line;

CIMGUI_API void ImGuiMarkdown_Line_Init(ImGuiMarkdown_Line *line);

typedef struct ImGuiMarkdown_TextBlock {
	int start;
	int stop;
} ImGuiMarkdown_TextBlock;

CIMGUI_API int ImGuiMarkdown_TextBlock_size(const ImGuiMarkdown_TextBlock *block);

typedef enum ImGuiMarkdown_LinkState {
	ImGuiMarkdown_LinkState_NO_LINK,
	ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKET_OPEN,
	ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKETS,
	ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKETS_ROUND_BRACKET_OPEN,
} ImGuiMarkdown_LinkState;

typedef struct ImGuiMarkdown_Link {
	ImGuiMarkdown_LinkState state;
	ImGuiMarkdown_TextBlock text;
	ImGuiMarkdown_TextBlock url;
	bool					isImage;
	int						num_brackets_open;
} ImGuiMarkdown_Link;

CIMGUI_API void ImGuiMarkdown_Link_Init(ImGuiMarkdown_Link *link);

typedef enum ImGuiMarkdown_EmphasisState {
	ImGuiMarkdown_EmphasisState_NONE,
	ImGuiMarkdown_EmphasisState_LEFT,
	ImGuiMarkdown_EmphasisState_MIDDLE,
	ImGuiMarkdown_EmphasisState_RIGHT,
} ImGuiMarkdown_EmphasisState;

typedef struct ImGuiMarkdown_Emphasis {
	ImGuiMarkdown_EmphasisState state;
	ImGuiMarkdown_TextBlock		text;
	char						sym;
} ImGuiMarkdown_Emphasis;

CIMGUI_API void ImGuiMarkdown_Emphasis_Init(ImGuiMarkdown_Emphasis *em);

// Helper: IsCharInsideWord
CIMGUI_API bool ImGuiMarkdown_IsCharInsideWord(char c);

#ifdef __cplusplus
}
#endif
#endif // CIMGUI_MARKDOWN_H