/* Based off https://github.com/enkisoftware/imgui_markdown
 * Copyright (c) 2019 Juliette Foucaut and Doug Binks
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgement in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui_markdown.h"
#include "cimgui.h"
#include <string.h>
#include <stdio.h>

#if defined(_MSC_VER) && !defined(snprintf)
#define snprintf _snprintf
#endif

// Helper to initialize ImGuiMarkdown_ImageData to default values
void ImGuiMarkdown_ImageData_Init(ImGuiMarkdown_ImageData *data)
{
	data->isValid		  = false;
	data->useLinkCallback = false;
	data->user_texture_id = (ImTextureRef_c){0};
	data->size			  = *ImVec2_ImVec2_Float(100.0f, 100.0f);
	data->uv0			  = *ImVec2_ImVec2_Float(0.0f, 0.0f);
	data->uv1			  = *ImVec2_ImVec2_Float(1.0f, 1.0f);
	data->tint_col		  = *ImVec4_ImVec4_Float(1.0f, 1.0f, 1.0f, 1.0f);
	data->border_col	  = *ImVec4_ImVec4_Float(0.0f, 0.0f, 0.0f, 0.0f);
	data->bg_col		  = *ImVec4_ImVec4_Float(0.0f, 0.0f, 0.0f, 0.0f);
}

// Helper to initialize ImGuiMarkdown_Config to default values
void ImGuiMarkdown_Config_Init(ImGuiMarkdown_Config *config)
{
	config->linkCallback	= NULL;
	config->tooltipCallback = ImGuiMarkdown_DefaultTooltipCallback; // Default
	config->imageCallback	= NULL;
	config->linkIcon		= ""; // Default empty
	for (int i = 0; i < IMGUI_MARKDOWN_NUM_HEADINGS; ++i) {
		config->headingFormats[i].font		= NULL;
		config->headingFormats[i].separator = true; // Default from original
	}
	// Ensure H3 by default doesn't have a separator to match original example setup more closely
	if (IMGUI_MARKDOWN_NUM_HEADINGS >= 3) {
		config->headingFormats[2].separator = false;
	}
	config->userData	   = NULL;
	config->formatCallback = ImGuiMarkdown_DefaultFormatCallback; // Default
}

void ImGuiMarkdown_Line_Init(ImGuiMarkdown_Line *line)
{
	line->isHeading			   = false;
	line->isEmphasis		   = false;
	line->isUnorderedListStart = false;
	line->isLeadingSpace	   = true;
	line->leadSpaceCount	   = 0;
	line->headingCount		   = 0;
	line->emphasisCount		   = 0;
	line->lineStart			   = 0;
	line->lineEnd			   = 0;
	line->lastRenderPosition   = 0;
}

int ImGuiMarkdown_TextBlock_size(const ImGuiMarkdown_TextBlock *block)
{
	return block->stop - block->start;
}

void ImGuiMarkdown_Link_Init(ImGuiMarkdown_Link *link)
{
	link->state		 = ImGuiMarkdown_LinkState_NO_LINK;
	link->text.start = link->text.stop = 0;
	link->url.start = link->url.stop = 0;
	link->isImage					 = false;
	link->num_brackets_open			 = 0;
}

void ImGuiMarkdown_Emphasis_Init(ImGuiMarkdown_Emphasis *em)
{
	em->state	   = ImGuiMarkdown_EmphasisState_NONE;
	em->text.start = em->text.stop = 0;
	em->sym						   = '\0';
}

void ImGuiMarkdown_DefaultTooltipCallback(ImGuiMarkdown_TooltipCallbackData data)
{
	if (data.linkData.isImage) {
		igSetTooltip(" %. *s", data.linkData.linkLength, data.linkData.link);
	} else {
		// Ensure linkIcon is not null
		const char *icon = data.linkIcon ? data.linkIcon : "";
		// You might need a sufficiently large buffer for the tooltip
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%s Open in browser\n%.*s", icon, data.linkData.linkLength,
				 data.linkData.link);
		igSetTooltip(buffer);
	}
}

void ImGuiMarkdown_UnderLine(ImU32 col)
{
	ImVec2 min = igGetItemRectMin();
	ImVec2 max = igGetItemRectMax();
	min.y	   = max.y;
	ImDrawList_AddLine(igGetWindowDrawList(), min, max, col, 1.0f);
}

// TextRegion methods converted to C functions
void ImGuiMarkdown_TextRegion_Init(ImGuiMarkdown_TextRegion *region)
{
	region->indentX = 0.0f;
}

void ImGuiMarkdown_TextRegion_RenderTextWrapped(ImGuiMarkdown_TextRegion *region, const char *text,
												const char *text_end, bool bIndentToHere)
{
	ImVec2 contentRegionAvail = igGetContentRegionAvail();
	float  widthLeft		  = contentRegionAvail.x;

	const char *endLine = ImFont_CalcWordWrapPosition(igGetFont(), igGetFont()->LegacySize, text, text_end, widthLeft);
	igTextUnformatted(text, endLine);

	if (bIndentToHere) {
		ImVec2 newContentRegionAvail = igGetContentRegionAvail();
		float  indentNeeded			 = widthLeft - newContentRegionAvail.x; // Corrected calculation
		if (indentNeeded > 0.0f) {											// Check if indent is actually needed
			igIndent(indentNeeded);
			region->indentX += indentNeeded;
		}
	}

	ImVec2 contentRegionAvailAfterIndent = igGetContentRegionAvail();
	widthLeft							 = contentRegionAvailAfterIndent.x;

	while (endLine < text_end) {
		text = endLine;
		if (*text == ' ') {
			++text;
		}
		endLine = ImFont_CalcWordWrapPosition(igGetFont(), igGetFont()->LegacySize, text, text_end, widthLeft);
		if (text == endLine && endLine < text_end) { // Avoid infinite loop on single char that cannot fit
			endLine++;
		}
		igTextUnformatted(text, endLine);
	}
}

void ImGuiMarkdown_TextRegion_RenderListTextWrapped(ImGuiMarkdown_TextRegion *region, const char *text,
													const char *text_end)
{
	igBullet();
	igSameLine(0.0f, 0.0f);
	ImGuiMarkdown_TextRegion_RenderTextWrapped(region, text, text_end, true);
}

void ImGuiMarkdown_TextRegion_ResetIndent(ImGuiMarkdown_TextRegion *region)
{
	if (region->indentX > 0.0f) {
		igUnindent(region->indentX);
	}
	region->indentX = 0.0f;
}

void ImGuiMarkdown_RenderLine(const char *markdown, ImGuiMarkdown_Line *line, ImGuiMarkdown_TextRegion *textRegion,
							  const ImGuiMarkdown_Config *mdConfig)
{
	int indentStart = 0;
	if (line->isUnorderedListStart) {
		indentStart = 1;
	}
	for (int j = indentStart; j < line->leadSpaceCount / 2; ++j) {
		igIndent(0.0f); // igIndent takes width, 0.0f means default indent
	}

	ImGuiMarkdown_FormatInfo formatInfo;
	memset(&formatInfo, 0, sizeof(ImGuiMarkdown_FormatInfo)); // Zero initialize
	formatInfo.config = mdConfig;

	int textStart = line->lastRenderPosition + 1;
	int textSize  = line->lineEnd - textStart;

	// Ensure text and text_end are valid
	const char *text	 = markdown + textStart;
	const char *text_end = markdown + line->lineEnd; // text + textSize;

	if (line->isUnorderedListStart) {
		formatInfo.type = ImGuiMarkdown_FormatType_UNORDERED_LIST;
		mdConfig->formatCallback(&formatInfo, true);
		// For list, the first character after '* ' is the actual start of the text
		ImGuiMarkdown_TextRegion_RenderListTextWrapped(textRegion, text + 1, text_end); // Adjust for '* '
	} else if (line->isHeading) {
		formatInfo.level = line->headingCount;
		formatInfo.type	 = ImGuiMarkdown_FormatType_HEADING;
		mdConfig->formatCallback(&formatInfo, true);
		// For heading, the first character after '# ' is the actual start
		ImGuiMarkdown_TextRegion_RenderTextWrapped(textRegion, text + 1, text_end, false); // Adjust for '# '
	} else if (line->isEmphasis) {
		formatInfo.level = line->emphasisCount; // 1 for normal, 2 for strong
		formatInfo.type	 = ImGuiMarkdown_FormatType_EMPHASIS;
		mdConfig->formatCallback(&formatInfo, true);
		// For emphasis, text starts directly
		ImGuiMarkdown_TextRegion_RenderTextWrapped(textRegion, text, text_end, false);
	} else {
		formatInfo.type = ImGuiMarkdown_FormatType_NORMAL_TEXT;
		mdConfig->formatCallback(&formatInfo, true);
		ImGuiMarkdown_TextRegion_RenderTextWrapped(textRegion, text, text_end, false);
	}
	mdConfig->formatCallback(&formatInfo, false);

	for (int j = indentStart; j < line->leadSpaceCount / 2; ++j) {
		igUnindent(0.0f);
	}
}

bool ImGuiMarkdown_TextRegion_RenderLinkText(ImGuiMarkdown_TextRegion *region, const char *text, const char *text_end,
											 const ImGuiMarkdown_Link *link_data, const char *markdown,
											 const ImGuiMarkdown_Config *mdConfig, const char **linkHoverStart)
{
	(void)region; // Not used in this specific function
	ImGuiMarkdown_FormatInfo formatInfo;
	memset(&formatInfo, 0, sizeof(ImGuiMarkdown_FormatInfo));
	formatInfo.config = mdConfig;
	formatInfo.type	  = ImGuiMarkdown_FormatType_LINK;
	mdConfig->formatCallback(&formatInfo, true);

	igPushTextWrapPos(-1.0f);
	igTextUnformatted(text, text_end);
	igPopTextWrapPos();

	bool bThisItemHovered = igIsItemHovered(ImGuiHoveredFlags_None);
	if (bThisItemHovered) {
		*linkHoverStart = markdown + link_data->text.start;
	}
	bool bHovered = bThisItemHovered || (*linkHoverStart == (markdown + link_data->text.start));

	formatInfo.itemHovered = bHovered;
	mdConfig->formatCallback(&formatInfo, false);

	if (bHovered) {
		if (igIsMouseReleased_Nil(0) && mdConfig->linkCallback) {
			ImGuiMarkdown_LinkCallbackData cbData = {markdown + link_data->text.start,
													 ImGuiMarkdown_TextBlock_size(&link_data->text),
													 markdown + link_data->url.start,
													 ImGuiMarkdown_TextBlock_size(&link_data->url),
													 mdConfig->userData,
													 false};
			mdConfig->linkCallback(cbData);
		}
		if (mdConfig->tooltipCallback) {
			ImGuiMarkdown_TooltipCallbackData tooltipData = {
				{markdown + link_data->text.start, ImGuiMarkdown_TextBlock_size(&link_data->text),
				 markdown + link_data->url.start, ImGuiMarkdown_TextBlock_size(&link_data->url), mdConfig->userData,
				 false},
				mdConfig->linkIcon};
			mdConfig->tooltipCallback(tooltipData);
		}
	}
	return bThisItemHovered;
}

bool ImGuiMarkdown_IsCharInsideWord(char c)
{
	return c != ' ' && c != '.' && c != ',' && c != ';' && c != '!' && c != '?' && c != '\"';
}

void ImGuiMarkdown_TextRegion_RenderLinkTextWrapped(ImGuiMarkdown_TextRegion *region, const char *text,
													const char *text_end, const ImGuiMarkdown_Link *link_data,
													const char *markdown, const ImGuiMarkdown_Config *mdConfig,
													const char **linkHoverStart, bool bIndentToHere)
{
	ImVec2		contentRegionAvailAfterIndent = igGetContentRegionAvail();
	float		widthLeft					  = contentRegionAvailAfterIndent.x;
	const char *endLine						  = text;

	if (widthLeft > 0.0f) {
		endLine = ImFont_CalcWordWrapPosition(igGetFont(), igGetFont()->LegacySize, text, text_end, widthLeft);
	}

	if (endLine > text && endLine < text_end) {
		if (ImGuiMarkdown_IsCharInsideWord(*endLine)) {
			ImVec2		cursorPos	  = igGetCursorScreenPos();
			ImVec2		windowPos	  = igGetWindowPos();
			float		widthNextLine = widthLeft + cursorPos.x - windowPos.x;
			const char *endNextLine =
				ImFont_CalcWordWrapPosition(igGetFont(), igGetFont()->LegacySize, text, text_end, widthNextLine);
			if (endNextLine == text_end || (endNextLine <= text_end && !ImGuiMarkdown_IsCharInsideWord(*endNextLine))) {
				endLine = text;
			}
		}
	}

	bool bHovered =
		ImGuiMarkdown_TextRegion_RenderLinkText(region, text, endLine, link_data, markdown, mdConfig, linkHoverStart);

	if (bIndentToHere) {
		ImVec2 newContentRegionAvail = igGetContentRegionAvail();
		float  indentNeeded			 = widthLeft - newContentRegionAvail.x;
		if (indentNeeded > 0.0f) {
			igIndent(indentNeeded);
			region->indentX += indentNeeded;
		}
	}

	contentRegionAvailAfterIndent = igGetContentRegionAvail();
	widthLeft					  = contentRegionAvailAfterIndent.x;
	while (endLine < text_end) {
		text = endLine;
		if (*text == ' ') {
			++text;
		}
		endLine = ImFont_CalcWordWrapPosition(igGetFont(), igGetFont()->LegacySize, text, text_end, widthLeft);
		if (text == endLine && endLine < text_end) {
			endLine++;
		}
		bool bThisLineHovered = ImGuiMarkdown_TextRegion_RenderLinkText(region, text, endLine, link_data, markdown,
																		mdConfig, linkHoverStart);
		bHovered			  = bHovered || bThisLineHovered;
	}

	if (!bHovered && *linkHoverStart == (markdown + link_data->text.start)) {
		*linkHoverStart = NULL;
	}
}

// The main Markdown rendering function
void ImGuiMarkdown(const char *markdown, size_t markdownLength, const ImGuiMarkdown_Config *mdConfig)
{
	static const char *linkHoverStart = NULL;
	// ImGuiStyle* style = igGetStyle(); // Not directly used in original for markdown rendering logic itself

	ImGuiMarkdown_Line		 line;
	ImGuiMarkdown_Link		 link;
	ImGuiMarkdown_Emphasis	 em;
	ImGuiMarkdown_TextRegion textRegion;

	ImGuiMarkdown_Line_Init(&line);
	ImGuiMarkdown_Link_Init(&link);
	ImGuiMarkdown_Emphasis_Init(&em);
	ImGuiMarkdown_TextRegion_Init(&textRegion);

	line.lineStart = 0; // Initialize lineStart for the very first line

	char c = 0;
	for (int i = 0; i < (int)markdownLength; ++i) {
		c = markdown[i];
		if (c == 0) {
			break;
		}

		if (line.isLeadingSpace) {
			if (c == ' ') {
				++line.leadSpaceCount;
				continue;
			} else {
				line.isLeadingSpace		= false;
				line.lastRenderPosition = i - 1;
				if ((c == '*') && (line.leadSpaceCount >= 2)) {
					if (((int)markdownLength > i + 1) && (markdown[i + 1] == ' ')) {
						line.isUnorderedListStart = true;
						++i;
						++line.lastRenderPosition;
					}
				} else if (c == '#') {
					line.headingCount	   = 1; // Start with one '#'
					bool bContinueChecking = true;
					int	 j				   = i;
					while (++j < (int)markdownLength && bContinueChecking) {
						char temp_c = markdown[j];
						switch (temp_c) {
						case '#':
							line.headingCount++;
							break;
						case ' ':
							line.lastRenderPosition = j - 1; // Character before the space
							i						= j;	 // Advance main loop counter past the space
							line.isHeading			= true;
							bContinueChecking		= false;
							break;
						default:
							line.isHeading	  = false; // Not a valid heading if non-'#' non-' ' follows '#'s
							line.headingCount = 0;	   // Reset heading count
							bContinueChecking = false;
							break;
						}
					}
					if (line.isHeading) {
						ImGuiMarkdown_Emphasis_Init(&em); // Reset emphasis
						continue;
					}
					// If not a heading, fall through to process the '#' as normal text
				}
			}
		}

		// Link parsing
		switch (link.state) {
		case ImGuiMarkdown_LinkState_NO_LINK:
			if (c == '[' && !line.isHeading) {
				link.state		= ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKET_OPEN;
				link.text.start = i + 1;
				if (i > 0 && markdown[i - 1] == '!') {
					link.isImage = true;
				}
			}
			break;
		case ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKET_OPEN:
			if (c == ']') {
				link.state	   = ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKETS;
				link.text.stop = i;
			}
			break;
		case ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKETS:
			if (c == '(') {
				link.state			   = ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKETS_ROUND_BRACKET_OPEN;
				link.url.start		   = i + 1;
				link.num_brackets_open = 1;
			} else { // Invalid link, reset
				ImGuiMarkdown_Link_Init(&link);
			}
			break;
		case ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKETS_ROUND_BRACKET_OPEN:
			if (c == '(') {
				++link.num_brackets_open;
			} else if (c == ')') {
				--link.num_brackets_open;
			}
			if (link.num_brackets_open == 0) {
				ImGuiMarkdown_Emphasis_Init(&em);

				line.lineEnd = link.text.start - (link.isImage ? 2 : 1);
				if (line.lineEnd > line.lastRenderPosition + 1) { // Check if there's text to render before link
					ImGuiMarkdown_RenderLine(markdown, &line, &textRegion, mdConfig);
				}

				line.leadSpaceCount		  = 0; // Reset for parts after the link on the same visual line
				link.url.stop			  = i;
				line.isUnorderedListStart = false;
				igSameLine(0.0f, 0.0f);

				if (link.isImage) {
					bool drawnImage = false;
					bool useLinkCb	= false;
					if (mdConfig->imageCallback) {
						ImGuiMarkdown_LinkCallbackData cbData = {
							markdown + link.text.start, ImGuiMarkdown_TextBlock_size(&link.text),
							markdown + link.url.start,	ImGuiMarkdown_TextBlock_size(&link.url),
							mdConfig->userData,			true};
						ImGuiMarkdown_ImageData imageData = mdConfig->imageCallback(cbData);
						useLinkCb						  = imageData.useLinkCallback;
						if (imageData.isValid) {
							ImVec2 contentSize = igGetContentRegionAvail();
							if (imageData.size.x > contentSize.x) {
								float const ratio = imageData.size.y / imageData.size.x;
								imageData.size.x  = contentSize.x;
								imageData.size.y  = contentSize.x * ratio;
							}

							// CImGui version of ImageWithBg or equivalent logic
							// Simplified: just ImGui_Image for now, bg/border needs manual draw list if not in CImGui core
							if (imageData.bg_col.w > 0.0f) {
								ImVec2 p_max;
								ImVec2 p_min = igGetCursorScreenPos();
								p_max.x		 = p_min.x + imageData.size.x;
								p_max.y		 = p_min.y + imageData.size.y;
								ImDrawList_AddRectFilled(igGetWindowDrawList(), p_min, p_max,
														 igGetColorU32_Vec4(imageData.bg_col), 0.0f, ImDrawFlags_None);
							}
							igImage(imageData.user_texture_id, imageData.size, imageData.uv0, imageData.uv1);
							drawnImage = true;
						}
					}
					if (!drawnImage) {
						igText("( Image %.*s not loaded )", ImGuiMarkdown_TextBlock_size(&link.url),
							   markdown + link.url.start);
					}
					if (igIsItemHovered(ImGuiHoveredFlags_None)) {
						if (igIsMouseReleased_Nil(0) && mdConfig->linkCallback && useLinkCb) {
							ImGuiMarkdown_LinkCallbackData cbData = {
								markdown + link.text.start, ImGuiMarkdown_TextBlock_size(&link.text),
								markdown + link.url.start,	ImGuiMarkdown_TextBlock_size(&link.url),
								mdConfig->userData,			true};
							mdConfig->linkCallback(cbData);
						}
						if (ImGuiMarkdown_TextBlock_size(&link.text) > 0 && mdConfig->tooltipCallback) {
							ImGuiMarkdown_TooltipCallbackData tooltipData = {
								{markdown + link.text.start, ImGuiMarkdown_TextBlock_size(&link.text),
								 markdown + link.url.start, ImGuiMarkdown_TextBlock_size(&link.url), mdConfig->userData,
								 true},
								mdConfig->linkIcon};
							mdConfig->tooltipCallback(tooltipData);
						}
					}
				} else {
					ImGuiMarkdown_TextRegion_RenderLinkTextWrapped(&textRegion, markdown + link.text.start,
																   markdown +
																	   link.text.stop, // Use text.stop for text_end
																   &link, markdown, mdConfig, &linkHoverStart, false);
				}
				igSameLine(0.0f, 0.0f);
				ImGuiMarkdown_Link_Init(&link);
				line.lastRenderPosition = i;
				line.lineStart			= i + 1; // Next segment starts after the link
			}
			break; // Must break from switch
		}
		if (link.state != ImGuiMarkdown_LinkState_NO_LINK &&
			link.state != ImGuiMarkdown_LinkState_HAS_SQUARE_BRACKETS_ROUND_BRACKET_OPEN)
			continue; // If in middle of link parsing

		// Emphasis parsing
		switch (em.state) {
		case ImGuiMarkdown_EmphasisState_NONE:
			if (link.state == ImGuiMarkdown_LinkState_NO_LINK && !line.isHeading) {
				int next = i + 1;
				int prev = i - 1;
				if ((c == '*' || c == '_') &&
					(i == line.lineStart || markdown[prev] == ' ' || markdown[prev] == '\t') &&
					(int)markdownLength > next && markdown[next] != ' ' && markdown[next] != '\n' &&
					markdown[next] != '\t') {
					em.state		   = ImGuiMarkdown_EmphasisState_LEFT;
					em.sym			   = c;
					em.text.start	   = i; // Start of emphasis symbols
					line.emphasisCount = 1;
					continue;
				}
			}
			break;
		case ImGuiMarkdown_EmphasisState_LEFT:
			if (em.sym == c) {
				++line.emphasisCount;
				continue;
			} else {
				em.text.start = i; // Actual text starts here
				em.state	  = ImGuiMarkdown_EmphasisState_MIDDLE;
			}
			break;
		case ImGuiMarkdown_EmphasisState_MIDDLE:
			if (em.sym == c) {
				em.state	 = ImGuiMarkdown_EmphasisState_RIGHT;
				em.text.stop = i; // Text ends before this symbol
			} else {
				break; // continue consuming text
			}
			// Fallthrough for C++11, explicit for C
			if (em.state != ImGuiMarkdown_EmphasisState_RIGHT) break;
			/* FALLTHROUGH */
		case ImGuiMarkdown_EmphasisState_RIGHT:
			if (em.sym == c) { // Consuming closing emphasis symbols
				if (line.emphasisCount < 3 && (i - em.text.stop + 1 == line.emphasisCount)) {
					// Render text up to emphasis
					int preEmphasisEnd = em.text.start - line.emphasisCount;
					if (preEmphasisEnd > line.lastRenderPosition + 1) {
						line.lineEnd = preEmphasisEnd;
						ImGuiMarkdown_RenderLine(markdown, &line, &textRegion, mdConfig);
						igSameLine(0.0f, 0.0f);
						line.isUnorderedListStart = false; // Subsequent part isn't a new list item
						line.leadSpaceCount		  = 0;
					}

					line.isEmphasis			= true;
					line.lastRenderPosition = em.text.start - line.emphasisCount - 1;
					line.lineStart			= em.text.start; // Text part of emphasis
					line.lineEnd			= em.text.stop;
					ImGuiMarkdown_RenderLine(markdown, &line, &textRegion, mdConfig);
					igSameLine(0.0f, 0.0f);

					line.isEmphasis			= false;
					line.lastRenderPosition = i;
					line.lineStart			= i + 1; // Next segment starts after emphasis
					ImGuiMarkdown_Emphasis_Init(&em);
				}
				continue; // Continue consuming closing symbols or check next char
			} else {	  // Symbol doesn't match, emphasis broken or ended
				em.state = ImGuiMarkdown_EmphasisState_NONE;
				// The text consumed was not part of a valid emphasis.
				// Rewind or re-evaluate 'i' if needed, or let it be part of normal text block.
				// Original logic seems to let it fall through to be part of normal text.
			}
			break;
		}

		// Handle end of line
		if (c == '\n') {
			line.lineEnd = i;
			if (em.state == ImGuiMarkdown_EmphasisState_MIDDLE && line.emphasisCount >= 3 &&
				(line.lineStart + line.emphasisCount) == i && line.leadSpaceCount == 0) { // Check leadSpaceCount for HR
				igSeparator();
			} else {
				if (line.lineEnd > line.lastRenderPosition + 1) { // Check if there's text to render
					ImGuiMarkdown_RenderLine(markdown, &line, &textRegion, mdConfig);
				}
			}

			ImGuiMarkdown_Line_Init(&line);
			ImGuiMarkdown_Emphasis_Init(&em);
			ImGuiMarkdown_Link_Init(&link); // Reset link state on new line too

			line.lineStart			= i + 1;
			line.lastRenderPosition = i;
			ImGuiMarkdown_TextRegion_ResetIndent(&textRegion);
		}
	}

	// Render any remaining text
	if (line.lineStart < (int)markdownLength) {
		if (em.state == ImGuiMarkdown_EmphasisState_LEFT && line.emphasisCount >= 3 && line.leadSpaceCount == 0 &&
			(line.lineStart + line.emphasisCount == (int)markdownLength)) { // HR at EOF
			igSeparator();
		} else {
			line.lineEnd = (int)markdownLength;
			// if (markdown[line.lineEnd - 1] == '\0' && line.lineEnd > line.lineStart) { // If null-terminated, don't include null
			//    --line.lineEnd;
			// }
			if (line.lineEnd > line.lastRenderPosition + 1) {
				ImGuiMarkdown_RenderLine(markdown, &line, &textRegion, mdConfig);
			}
		}
	}
	ImGuiMarkdown_TextRegion_ResetIndent(&textRegion); // Ensure any final indents are cleared
}

void ImGuiMarkdown_DefaultFormatCallback(const ImGuiMarkdown_FormatInfo *markdownFormatInfo, bool start)
{
	switch (markdownFormatInfo->type) {
	case ImGuiMarkdown_FormatType_NORMAL_TEXT:
		break;
	case ImGuiMarkdown_FormatType_EMPHASIS: {
		ImGuiMarkdown_HeadingFormat fmt;
		if (markdownFormatInfo->level == 1) { // Normal emphasis
			if (start) {
				ImVec4 colDisabled = *igGetStyleColorVec4(ImGuiCol_TextDisabled);
				igPushStyleColor_Vec4(ImGuiCol_Text, colDisabled);
			} else {
				igPopStyleColor(1);
			}
		} else { // Strong emphasis (level 2 or more)
			// Use H3 style for strong emphasis as a default
			fmt = markdownFormatInfo->config->headingFormats[IMGUI_MARKDOWN_NUM_HEADINGS - 1];
			if (start) {
				if (fmt.font) {
					igPushFont(fmt.font, 16); //TODO: ADD TO STRUCT
				}
			} else {
				if (fmt.font) {
					igPopFont();
				}
			}
		}
		break;
	}
	case ImGuiMarkdown_FormatType_HEADING: {
		ImGuiMarkdown_HeadingFormat fmt;
		if (markdownFormatInfo->level > IMGUI_MARKDOWN_NUM_HEADINGS) {
			fmt = markdownFormatInfo->config->headingFormats[IMGUI_MARKDOWN_NUM_HEADINGS - 1];
		} else {
			fmt = markdownFormatInfo->config->headingFormats[markdownFormatInfo->level - 1];
		}
		if (start) {
			if (fmt.font) {
				igPushFont(fmt.font, 16); //TODO: ADD TO STRUCT
			}
			igNewLine();
		} else {
			if (fmt.separator) {
				igSeparator();
				// igNewLine(); // Original has this, check if desired
			} else {
				igNewLine();
			}
			if (fmt.font) {
				igPopFont();
			}
		}
		break;
	}
	case ImGuiMarkdown_FormatType_UNORDERED_LIST:
		// Handled by RenderListTextWrapped using igBullet()
		break;
	case ImGuiMarkdown_FormatType_LINK:
		if (start) {
			ImVec4 colHovered = *igGetStyleColorVec4(ImGuiCol_ButtonHovered);
			igPushStyleColor_Vec4(ImGuiCol_Text, colHovered);
		} else {
			igPopStyleColor(1);
			if (markdownFormatInfo->itemHovered) {
				ImVec4 colHovered = *igGetStyleColorVec4(ImGuiCol_ButtonHovered);
				ImGuiMarkdown_UnderLine(igGetColorU32_Vec4(colHovered));
			} else {
				ImVec4 colButton = *igGetStyleColorVec4(ImGuiCol_Button);
				ImGuiMarkdown_UnderLine(igGetColorU32_Vec4(colButton));
			}
		}
		break;
	}
}