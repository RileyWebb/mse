/* Based off https://github.com/Teselka/imgui_hex_editor 
 * 
 * MIT License
 * 
 * Copyright (c) 2024 Teselka
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. 
 */

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui_hex.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>

#ifndef IM_MAX
#define IM_MAX(A, B) (((A) >= (B)) ? (A) : (B))
#endif

#ifndef IM_MIN
#define IM_MIN(A, B) (((A) <= (B)) ? (A) : (B))
#endif

#ifndef IM_ASSERT
#define IM_ASSERT(_EXPR) assert(_EXPR)
#endif

static ImU32 CalcContrastColor(ImU32 color)
{
    int r = (color >> 24) & 0xFF;
    int g = (color >> 16) & 0xFF;
    int b = (color >> 8) & 0xFF;

    const float l = (0.299f * r + 0.587f * g + 0.114f * b) / 255.f;
    const int c = l > 0.5f ? 0 : 255;
    return c | (c << 8) | (c << 16) | (color & 0xFF000000);
}

static char HalfByteToPrintable(unsigned char half_byte, bool lower)
{
    IM_ASSERT(!(half_byte & 0xf0));
    return half_byte <= 9 ? '0' + half_byte : (lower ? 'a' : 'A') + half_byte - 10;
}

static unsigned char KeyToHalfByte(ImGuiKey key)
{
    IM_ASSERT((key >= ImGuiKey_A && key <= ImGuiKey_F) || (key >= ImGuiKey_0 && key <= ImGuiKey_9));
    return (key >= ImGuiKey_A && key <= ImGuiKey_F) ? (char)(key - ImGuiKey_A) + 10 : (char)(key - ImGuiKey_0);
}

static bool HasAsciiRepresentation(unsigned char byte)
{
    return (byte >= '!' && byte <= '~');
}

static int CalcBytesPerLine(float bytes_avail_x, const ImVec2 byte_size, const ImVec2 spacing, bool show_ascii, const ImVec2 char_size, int separators)
{
    const float byte_width = byte_size.x + spacing.x + (show_ascii ? char_size.x : 0.f);
    int bytes_per_line = (int)(bytes_avail_x / byte_width);
    bytes_per_line = bytes_per_line <= 0 ? 1 : bytes_per_line;

    int actual_separators = separators > 0 ? (int)(bytes_per_line / separators) : 0;
    if (actual_separators != 0 && separators > 0 && bytes_per_line > actual_separators && (bytes_per_line - 1) % actual_separators == 0)
        --actual_separators;
    
    return separators > 0 ? CalcBytesPerLine(bytes_avail_x - (actual_separators * spacing.x), byte_size, spacing, show_ascii, char_size, 0) : bytes_per_line;
}

static bool RangeRangeIntersection(int a_min, int a_max, int b_min, int b_max, int* out_min, int* out_max)
{
    if (a_max < b_min || b_max < a_min)
        return false;

    *out_min = IM_MAX(a_min, b_min);
    *out_max = IM_MIN(a_max, b_max);

    if (*out_min <= *out_max)
        return true;

    return false;
}

static void RenderRectCornerCalcRounding(const ImVec2 ra, const ImVec2 rb, float* rounding)
{
    *rounding = IM_MIN(*rounding, fabsf(rb.x - ra.x) * 0.5f);
    *rounding = IM_MIN(*rounding, fabsf(rb.y - ra.y) * 0.5f);
}

static void RenderTopLeftCornerRect(ImDrawList* draw_list, const ImVec2 a, const ImVec2 b, ImU32 color, float rounding)
{
    const ImVec2 ra = { a.x + 0.5f, a.y + 0.5f };
    const ImVec2 rb = { b.x, b.y };

    RenderRectCornerCalcRounding(ra, rb, &rounding);

    ImDrawList_PathArcToFast(draw_list, (ImVec2){ ra.x, rb.y }, 0, 3, 6);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ ra.x + rounding, ra.y + rounding }, rounding, 6, 9);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ rb.x , ra.y }, 0, 9, 12);

    ImDrawList_PathStroke(draw_list, color, ImDrawFlags_None, 1.f);
}

static void RenderBottomRightCornerRect(ImDrawList* draw_list, const ImVec2 a, const ImVec2 b, ImU32 color, float rounding)
{
    const ImVec2 ra = { a.x, a.y + 0.5f };
    const ImVec2 rb = { b.x - 0.5f, b.y + 0.5f };

    RenderRectCornerCalcRounding(ra, rb, &rounding);

    ImDrawList_PathArcToFast(draw_list, (ImVec2){ rb.x, ra.y }, 0, 9, 12);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ rb.x - rounding, rb.y - rounding }, rounding, 0, 3);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ ra.x, rb.y }, 0, 3, 6);

    ImDrawList_PathStroke(draw_list, color, ImDrawFlags_None, 1.f);
}

static void RenderTopRightCornerRect(ImDrawList* draw_list, const ImVec2 a, const ImVec2 b, ImU32 color, float rounding)
{
    const ImVec2 ra = { a.x + 0.5f, a.y + 0.5f };
    const ImVec2 rb = { b.x - 0.5f, b.y };

    RenderRectCornerCalcRounding(ra, rb, &rounding);

    ImDrawList_PathArcToFast(draw_list, ra, 0.f, 6, 9);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ rb.x - rounding, ra.y + rounding }, rounding, 9, 12);
    ImDrawList_PathArcToFast(draw_list, rb, 0.f, 0, 3);

    ImDrawList_PathStroke(draw_list, color, ImDrawFlags_None, 1.f);
}

static void RenderBottomLeftCornerRect(ImDrawList* draw_list, const ImVec2 a, const ImVec2 b, ImU32 color, float rounding)
{
    const ImVec2 ra = { a.x + 0.5f, a.y + 0.5f };
    const ImVec2 rb = { b.x + 0.5f, b.y + 0.5f };

    RenderRectCornerCalcRounding(ra, rb, &rounding);

    ImDrawList_PathArcToFast(draw_list, (ImVec2){ rb.x, rb.y }, 0.f, 0, 3);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ ra.x + rounding, rb.y - rounding }, rounding, 3, 6);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ ra.x, ra.y }, 0.f, 9, 12);
    
    ImDrawList_PathStroke(draw_list, color, ImDrawFlags_None, 1.f);
}

static void RenderBottomCornerRect(ImDrawList* draw_list, const ImVec2 a, const ImVec2 b, ImU32 color, float rounding)
{
    const ImVec2 ra = { a.x + 0.5f, a.y + 0.5f };
    const ImVec2 rb = { b.x + 0.5f, b.y + 0.5f };

    RenderRectCornerCalcRounding(ra, rb, &rounding);

    ImDrawList_PathArcToFast(draw_list, (ImVec2){ rb.x, ra.y }, 0.f, 0, 3);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ rb.x - rounding, rb.y - rounding }, rounding, 0, 3);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ ra.x + rounding, rb.y - rounding }, rounding, 3, 6);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ ra.x, ra.y }, 0.f, 9, 12);
    
    ImDrawList_PathStroke(draw_list, color, ImDrawFlags_None, 1.f);
}

static void RenderTopCornerRect(ImDrawList* draw_list, const ImVec2 a, const ImVec2 b, ImU32 color, float rounding)
{
    const ImVec2 ra = { a.x + 0.5f, a.y + 0.5f };
    const ImVec2 rb = { b.x - 0.5f, b.y + 0.5f };

    RenderRectCornerCalcRounding(ra, rb, &rounding);

    ImDrawList_PathArcToFast(draw_list, (ImVec2){ ra.x, rb.y }, 0.f, 3, 6);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ ra.x + rounding, ra.y + rounding }, rounding, 6, 9);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ rb.x - rounding, ra.y + rounding }, rounding, 9, 12);
    ImDrawList_PathArcToFast(draw_list, (ImVec2){ rb.x , rb.y }, 0.f, 0, 3);

    ImDrawList_PathStroke(draw_list, color, ImDrawFlags_None, 1.f);
}

static void RenderByteDecorations(ImDrawList* draw_list, const ImRect bb, ImU32 bg_color,
    ImGuiHexEditorHighlightFlags flags, ImU32 border_color, float rounding,
    int offset, int range_min, int range_max, int bytes_per_line, int i, int line_base)
{
    const bool has_border = flags & ImGuiHexEditorHighlightFlags_Border;

    if (!has_border) 
    {
        ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, 0.f, 0);
        return;
    }

    if (range_min == range_max)
    {
        ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, 0);
        ImDrawList_AddRect(draw_list, bb.Min, bb.Max, border_color, rounding, 0, 1.0f);
        return;
    }

    const int start_line = range_min / bytes_per_line;
    const int end_line = range_max / bytes_per_line;
    const int current_line = line_base / bytes_per_line;

    const bool is_start_line = start_line == (line_base / bytes_per_line);
    const bool is_end_line = end_line == (line_base / bytes_per_line);
    const bool is_last_byte = i == (bytes_per_line - 1);

    bool rendered_bg = false;

    if (offset == range_min) 
    {
        if (!is_last_byte)
        {
            ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, ImDrawFlags_RoundCornersTopLeft);
            RenderTopLeftCornerRect(draw_list, bb.Min, bb.Max, border_color, rounding);

            if (start_line == end_line)
                ImDrawList_AddLine(draw_list, (ImVec2){ bb.Min.x, bb.Max.y }, (ImVec2){ bb.Max.x, bb.Max.y }, border_color, 1.0f);
        }
        else
        {
            ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, ImDrawFlags_RoundCornersTop);
            RenderTopCornerRect(draw_list, bb.Min, bb.Max, border_color, rounding);
        }

        rendered_bg = true;
    }
    else if (i == 0) 
    {
        if (is_end_line)
        {
            if (offset == range_max)
            {
                ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, ImDrawFlags_RoundCornersBottom);
                RenderBottomCornerRect(draw_list, bb.Min, bb.Max, border_color, rounding);
            }
            else
            {
                ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, ImDrawFlags_RoundCornersBottomLeft);
                RenderBottomLeftCornerRect(draw_list, bb.Min, bb.Max, border_color, rounding);
            }
        
            rendered_bg = true;
        }
        else if (current_line == start_line + 1 && (range_min % bytes_per_line) != 0)
        {
            ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, ImDrawFlags_RoundCornersTopLeft);
            RenderTopLeftCornerRect(draw_list, bb.Min, bb.Max, border_color, rounding);
            rendered_bg = true;
        }
        else
        {
            if (!rendered_bg)
            {
                ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, 0.f, 0);
                rendered_bg = true;
            }

            ImDrawList_AddLine(draw_list, (ImVec2){ bb.Min.x, bb.Min.y }, (ImVec2){ bb.Min.x, bb.Max.y }, border_color, 1.0f);
        }
    }

    if (i != 0 && offset == range_max) 
    {
        if (start_line == end_line)
        {
            ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, ImDrawFlags_RoundCornersTopRight);
            RenderTopRightCornerRect(draw_list, bb.Min, bb.Max, border_color, rounding);
            ImDrawList_AddLine(draw_list, (ImVec2){ bb.Min.x, bb.Max.y }, (ImVec2){ bb.Max.x, bb.Max.y }, border_color, 1.0f);
        }
        else
        {
            ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, ImDrawFlags_RoundCornersBottomRight);
            RenderBottomRightCornerRect(draw_list, bb.Min, bb.Max, border_color, rounding);
        }

        rendered_bg = true;
    }
    else if (is_last_byte && offset != range_min)
    {
        if (is_start_line)
        {
            ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, ImDrawFlags_RoundCornersTopRight);
            RenderTopRightCornerRect(draw_list, bb.Min, bb.Max, border_color, rounding);
            rendered_bg = true;
        }
        else if (current_line == end_line - 1 && (range_max % bytes_per_line) != bytes_per_line - 1)
        {
            ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, rounding, ImDrawFlags_RoundCornersBottomRight);
            RenderBottomRightCornerRect(draw_list, bb.Min, bb.Max, border_color, rounding);
            rendered_bg = true;
        }
        else
        {
            if (!rendered_bg)
            {
                ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, 0.f, 0);
                rendered_bg = true;
            }

            ImDrawList_AddLine(draw_list, (ImVec2){ bb.Max.x - 1.f, bb.Min.y }, (ImVec2){ bb.Max.x - 1.f, bb.Max.y }, border_color, 1.0f);
        }
    }

    if ((is_start_line && offset != range_min && !is_last_byte && offset != range_max)
        || (current_line == start_line + 1 && (i < (range_min % bytes_per_line) && i != 0)))
    {
        if (!rendered_bg)
        {
            ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, 0.f, 0);
            rendered_bg = true;
        }

        ImDrawList_AddLine(draw_list, (ImVec2){ bb.Min.x, bb.Min.y }, (ImVec2){ bb.Max.x, bb.Min.y }, border_color, 1.0f);
    }

    if ((is_end_line && offset != range_max && i != 0)
        || (current_line == end_line - 1 && (i > (range_max % bytes_per_line) && !is_last_byte)))
    {
        if (!rendered_bg)
        {
            ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, 0.f, 0);
            rendered_bg = true;
        }

        ImDrawList_AddLine(draw_list, (ImVec2){ bb.Min.x, bb.Max.y }, (ImVec2){ bb.Max.x, bb.Max.y }, border_color, 1.0f);
    }

    if (!rendered_bg)
        ImDrawList_AddRectFilled(draw_list, bb.Min, bb.Max, bg_color, 0.f, 0);
}

bool igBeginHexEditor(const char* str_id, ImGuiHexEditorState* state, ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
    if (!igBeginChild_Str(str_id, size, child_flags, window_flags))
        return false;

    ImVec2 char_size;
    igCalcTextSize(&char_size, "0", NULL, false, -1.0f);
    const ImVec2 byte_size = { char_size.x * 2.f, char_size.y };

    ImGuiStyle* style = igGetStyle();
    const ImVec2 spacing = style->ItemSpacing;

    ImVec2 content_avail;
    igGetContentRegionAvail(&content_avail);

    float address_max_size;
    int address_max_chars;
    if (state->ShowAddress)
    {
        int address_chars = state->AddressChars;
        if (address_chars == -1)
        {
            char temp_buf[64];
            address_chars = snprintf(temp_buf, sizeof(temp_buf), "%zX", (size_t)state->MaxBytes) + 1;
        }

        address_max_chars = address_chars + 1;
        address_max_size = char_size.x * address_max_chars + spacing.x * 0.5f;
    }
    else
    {
        address_max_size = 0.f;
        address_max_chars = 0;
    }

    float bytes_avail_x = content_avail.x - address_max_size;
    if (igGetScrollMaxY() > 0.f)
        bytes_avail_x -= style->ScrollbarSize;

    const bool show_ascii = state->ShowAscii;

    if (show_ascii)
        bytes_avail_x -= char_size.x * 0.5f;

    bytes_avail_x = bytes_avail_x < 0.f ? 0.f : bytes_avail_x;

    int bytes_per_line;

    if (state->BytesPerLine == -1)
    {
        bytes_per_line = CalcBytesPerLine(bytes_avail_x, byte_size, spacing, show_ascii, char_size, state->Separators);
    }
    else
    {
        bytes_per_line = state->BytesPerLine;
    }

    int actual_separators = 0;
    if (state->Separators > 0 && bytes_per_line > 0)
    {
        actual_separators = (int)(bytes_per_line / state->Separators);
        if (bytes_per_line % state->Separators == 0)
            --actual_separators;
    }
    
    int lines_count;
    if (bytes_per_line != 0)
    {
        lines_count = state->MaxBytes / bytes_per_line;
        if (lines_count * bytes_per_line < state->MaxBytes)
        {
            ++lines_count;
        }
    }
    else
        lines_count = 0;

    ImDrawList* draw_list = igGetWindowDrawList();
    ImGuiIO* io = igGetIO_Nil();

    const ImU32 text_color = igGetColorU32_Col(ImGuiCol_Text, 1.0f);
    const ImU32 text_disabled_color = igGetColorU32_Col(ImGuiCol_TextDisabled, 1.0f);
    const ImU32 text_selected_bg_color = igGetColorU32_Col(ImGuiCol_TextSelectedBg, 1.0f);
    const ImU32 separator_color = igGetColorU32_Col(ImGuiCol_Separator, 1.0f);
    const ImU32 border_color = igGetColorU32_Col(ImGuiCol_FrameBgActive, 1.0f);

    const bool lowercase_bytes = state->LowercaseBytes;

    const int select_start_byte = state->SelectStartByte;
    const int select_start_subbyte = state->SelectStartSubByte;
    const int select_end_byte = state->SelectEndByte;
    const int select_end_subbyte = state->SelectEndSubByte;
    const int last_selected_byte = state->LastSelectedByte;
    const int select_drag_byte = state->SelectDragByte;
    const int select_drag_subbyte = state->SelectDragSubByte;

    int next_select_start_byte = select_start_byte;
    int next_select_start_subbyte = select_start_subbyte;
    int next_select_end_byte = select_end_byte;
    int next_select_end_subbyte = select_end_subbyte;
    int next_last_selected_byte = last_selected_byte;
    int next_select_drag_byte = select_drag_byte;
    int next_select_drag_subbyte = select_drag_subbyte;

    ImGuiKey hex_key_pressed = ImGuiKey_None;

    if (state->EnableClipboard && igIsKeyChordPressed_Nil(ImGuiMod_Ctrl | ImGuiKey_C))
    {
        if (state->SelectStartByte != -1)
        {
            const int bytes_count = (state->SelectEndByte + 1) - state->SelectStartByte;

            char* bytes = (char*)igMemAlloc((size_t)bytes_count);
            if (bytes)
            {
                int read_bytes;

                if (state->ReadCallback)
                    read_bytes = state->ReadCallback(state, state->SelectStartByte, bytes, bytes_count);
                else
                {
                    memcpy(bytes, (char*)state->Bytes + state->SelectStartByte, bytes_count);
                    read_bytes = bytes_count;
                }

                if (read_bytes > 0)
                {
                    igLogToClipboard(0);

                    for (int i = 0, abs_i = state->SelectStartByte; i < bytes_count; i++, abs_i++)
                    {
                        const char byte = bytes[i];

                        char text[3];
                        text[0] = HalfByteToPrintable((byte & 0xf0) >> 4, lowercase_bytes);
                        text[1] = HalfByteToPrintable(byte & 0x0f, lowercase_bytes);
                        text[2] = '\0';

                        igLogText("%s", text);

                        if (bytes_per_line != 0 && ((abs_i % bytes_per_line) == bytes_per_line - 1) && abs_i != 0)
                        {
                            igLogText("\n");
                        }
                        else
                        {
                            igLogText(" ");
                        }
                    }

                    igLogFinish();
                }

                igMemFree(bytes);
            }
        }
    }
    else
    {
        if (last_selected_byte != -1)
        {
            bool any_pressed = false;
            if (igIsKeyPressed_Bool(ImGuiKey_LeftArrow, true))
            {
                if (!select_start_subbyte)
                {
                    if (last_selected_byte == 0)
                    {
                        next_last_selected_byte = 0;
                    }
                    else
                    {
                        next_last_selected_byte = last_selected_byte - 1;
                        next_select_start_subbyte = 1;
                    }
                }
                else
                    next_select_start_subbyte = 0;

                any_pressed = true;
            }
            else if (igIsKeyPressed_Bool(ImGuiKey_RightArrow, true))
            {
                if (select_start_subbyte)
                {
                    if (last_selected_byte >= state->MaxBytes - 1)
                    {
                        next_last_selected_byte = state->MaxBytes - 1;
                    }
                    else
                    {
                        next_last_selected_byte = last_selected_byte + 1;
                        next_select_start_subbyte = 0;
                    }
                }
                else
                    next_select_start_subbyte = 1;

                any_pressed = true;
            }
            else if (bytes_per_line != 0)
            {
                if (igIsKeyPressed_Bool(ImGuiKey_UpArrow, true))
                {
                    if (last_selected_byte >= bytes_per_line)
                    {
                        next_last_selected_byte = last_selected_byte - bytes_per_line;
                    }

                    any_pressed = true;
                }
                else if (igIsKeyPressed_Bool(ImGuiKey_DownArrow, true))
                {
                    if (last_selected_byte < state->MaxBytes - bytes_per_line)
                    {
                        next_last_selected_byte = last_selected_byte + bytes_per_line;
                    }

                    any_pressed = true;
                }
            }

            if (any_pressed)
            {
                next_select_start_byte = next_last_selected_byte;
                next_select_end_byte = next_last_selected_byte;
            }
        }

        for (ImGuiKey key = ImGuiKey_A; key != ImGuiKey_G; key = (ImGuiKey)((int)key + 1))
        {
            if (igIsKeyPressed_Bool(key, false))
            {
                hex_key_pressed = key;
                break;
            }
        }

        if (hex_key_pressed == ImGuiKey_None)
        {
            for (ImGuiKey key = ImGuiKey_0; key != ImGuiKey_A; key = (ImGuiKey)((int)key + 1))
            {
                if (igIsKeyPressed_Bool(key, false))
                {
                    hex_key_pressed = key;
                    break;
                }
            }
        }
    }

    unsigned char stack_line_buf[128];
    unsigned char* line_buf = bytes_per_line <= (int)sizeof(stack_line_buf) ? stack_line_buf : (unsigned char*)igMemAlloc(bytes_per_line);
    if (!line_buf)
        return true;

    char stack_address_buf[32];
    char* address_buf = address_max_chars <= (int)sizeof(stack_address_buf) ? stack_address_buf : (char*)igMemAlloc(address_max_chars);
    if (!address_buf)
    {
        if (line_buf != stack_line_buf)
            igMemFree(line_buf);

        return true;
    }

    ImVec2 mouse_pos;
    igGetMousePos(&mouse_pos);
    const bool mouse_left_down = igIsMouseDown_Nil(ImGuiMouseButton_Left);

    ImGuiListClipper clipper = {0};
    //memset(&clipper, 0, sizeof(clipper));
    ImGuiListClipper_Begin(&clipper, lines_count, byte_size.y + spacing.y);
    
    while (ImGuiListClipper_Step(&clipper))
    {
        const int clipper_lines = clipper.DisplayEnd - clipper.DisplayStart;

        ImVec2 cursor;
        igGetCursorScreenPos(&cursor);

        ImVec2 ascii_cursor = { cursor.x + address_max_size + (spacing.x * 0.5f) + (bytes_per_line * (byte_size.x + spacing.x)) + (actual_separators * spacing.x), cursor.y };
        if (show_ascii)
        {
            ImDrawList_AddLine(draw_list, ascii_cursor, (ImVec2){ ascii_cursor.x, ascii_cursor.y + clipper_lines * (byte_size.y + spacing.y) }, separator_color, 1.0f);
        }

        for (int n = clipper.DisplayStart; n != clipper.DisplayEnd; n++)
        {
            const int line_base = n * bytes_per_line;
            if (state->ShowAddress)
            {
                if (!state->GetAddressNameCallback || !state->GetAddressNameCallback(state, line_base, address_buf, address_max_chars))
                    snprintf(address_buf, (size_t)address_max_chars, "%0.*zX", address_max_chars - 1, (size_t)line_base);

                ImVec2 text_size;
                igCalcTextSize(&text_size, address_buf, NULL, false, -1.0f);
                ImDrawList_AddText_Vec2(draw_list, cursor, text_color, address_buf, NULL);
                ImDrawList_AddText_Vec2(draw_list, (ImVec2){ cursor.x + text_size.x, cursor.y }, text_disabled_color, ":", NULL);
                cursor.x += address_max_size;
            }

            int max_bytes_per_line = line_base;
            max_bytes_per_line = max_bytes_per_line > state->MaxBytes ? max_bytes_per_line - state->MaxBytes : bytes_per_line;

            int bytes_read;
            if (!state->ReadCallback)
            {
                memcpy(line_buf, (char*)state->Bytes + line_base, max_bytes_per_line);
                bytes_read = max_bytes_per_line;
            }
            else
                bytes_read = state->ReadCallback(state, line_base, line_buf, max_bytes_per_line);

            cursor.x += spacing.x * 0.5f;

            for (int i = 0; i != bytes_per_line; i++)
            {
                const ImRect byte_bb = { { cursor.x, cursor.y }, { cursor.x + byte_size.x, cursor.y + byte_size.y } };

                ImRect item_bb = byte_bb;

                item_bb.Min.x -= spacing.x * 0.5f;

                if (n != clipper.DisplayStart)
                    item_bb.Min.y -= spacing.y * 0.5f;

                item_bb.Max.x += spacing.x * 0.5f;
                item_bb.Max.y += spacing.y * 0.5f;

                const int offset = bytes_per_line * n + i;
                unsigned char byte;

                ImVec2 byte_ascii = ascii_cursor;

                byte_ascii.x += (char_size.x * i) + spacing.x;
                byte_ascii.y += (char_size.y + spacing.y) * (n - clipper.DisplayStart);

                char text[3];
                if (offset < state->MaxBytes && i < bytes_read)
                {
                    byte = line_buf[i];

                    text[0] = HalfByteToPrintable((byte & 0xf0) >> 4, lowercase_bytes);
                    text[1] = HalfByteToPrintable(byte & 0x0f, lowercase_bytes);
                    text[2] = '\0';
                }
                else
                {
                    byte = 0x00;

                    text[0] = '?';
                    text[1] = '?';
                    text[2] = '\0';
                }

                const ImGuiID id = igGetID_Int(offset);

                if (!igItemAdd(item_bb, id, NULL, ImGuiItemFlags_Inputable))
                    continue;

                ImU32 byte_text_color = (offset >= state->MaxBytes || (state->RenderZeroesDisabled && byte == 0x00) || i >= bytes_read) ? text_disabled_color : text_color;

                if (offset >= select_start_byte && offset <= select_end_byte)
                {
                    ImGuiHexEditorHighlightFlags flags = state->SelectionHighlightFlags;
                    
                    if (select_start_byte == select_end_byte)
                    {
                        flags &= ~ImGuiHexEditorHighlightFlags_FullSized;
                    }

                    ImRect bb = (flags & ImGuiHexEditorHighlightFlags_FullSized) ? item_bb : byte_bb;

                    if (select_start_byte == select_end_byte)
                    {
                        float center_x = (byte_bb.Min.x + byte_bb.Max.x) * 0.5f;
                        if (select_start_subbyte)
                            bb.Min.x = center_x;
                        else
                            bb.Max.x = center_x;
                    }

                    RenderByteDecorations(draw_list, bb, text_selected_bg_color, flags, border_color,
                        style->FrameRounding, offset, select_start_byte, select_end_byte, bytes_per_line, i, line_base);

                    if (flags & ImGuiHexEditorHighlightFlags_Ascii)
                    {
                        ImRect ascii_bb = { byte_ascii, { byte_ascii.x + char_size.x, byte_ascii.y + char_size.y } };
                        RenderByteDecorations(draw_list, ascii_bb, text_selected_bg_color, flags, border_color, 
                            style->FrameRounding, offset, offset, offset, bytes_per_line, i, line_base);
                    }
                }
                else
                {
                    bool single_highlight = false;

                    if (state->SingleHighlightCallback)
                    {
                        ImU32 color;
                        ImU32 custom_border_color;

                        ImGuiHexEditorHighlightFlags flags = state->SingleHighlightCallback(state, offset, 
                                &color, &byte_text_color, &custom_border_color);

                        if (flags & ImGuiHexEditorHighlightFlags_Apply)
                        {
                            ImU32 highlight_border_color;

                            if (flags & ImGuiHexEditorHighlightFlags_BorderAutomaticContrast)
                                highlight_border_color = CalcContrastColor(color);
                            else if (flags & ImGuiHexEditorHighlightFlags_OverrideBorderColor)
                                highlight_border_color = custom_border_color;
                            else
                                highlight_border_color = border_color;

                            single_highlight = true;

                            RenderByteDecorations(draw_list, (flags & ImGuiHexEditorHighlightFlags_FullSized) ? item_bb : byte_bb, color, flags, highlight_border_color,
                                style->FrameRounding, offset, offset, offset, bytes_per_line, i, line_base);

                            if (flags & ImGuiHexEditorHighlightFlags_Ascii)
                            {
                                ImRect ascii_bb = { byte_ascii, { byte_ascii.x + char_size.x, byte_ascii.y + char_size.y } };
                                RenderByteDecorations(draw_list, ascii_bb, color, flags, highlight_border_color,
                                    style->FrameRounding, offset, offset, offset, bytes_per_line, i, line_base);
                            }
                            
                            if (flags & ImGuiHexEditorHighlightFlags_TextAutomaticContrast)
                                byte_text_color = CalcContrastColor(color);
                        }
                    }

                    if (!single_highlight)
                    {
                        for (int j = 0; j != state->HighlightRanges.Size; j++)
                        {
                            ImGuiHexEditorHighlightRange* range = &state->HighlightRanges.Data[j];

                            if (line_base + i >= range->From && line_base + i <= range->To)
                            {
                                ImU32 highlight_border_color;

                                if (range->Flags & ImGuiHexEditorHighlightFlags_BorderAutomaticContrast)
                                    highlight_border_color = CalcContrastColor(range->Color);
                                else if (range->Flags & ImGuiHexEditorHighlightFlags_OverrideBorderColor)
                                    highlight_border_color = range->BorderColor;
                                else
                                    highlight_border_color = border_color;

                                RenderByteDecorations(draw_list, (range->Flags & ImGuiHexEditorHighlightFlags_FullSized) ? item_bb : byte_bb, range->Color, range->Flags, highlight_border_color,
                                    style->FrameRounding, offset, range->From, range->To, bytes_per_line, i, line_base);

                                if (range->Flags & ImGuiHexEditorHighlightFlags_Ascii)
                                {
                                    ImRect ascii_bb = { byte_ascii, { byte_ascii.x + char_size.x, byte_ascii.y + char_size.y } };
                                    RenderByteDecorations(draw_list, ascii_bb, range->Color, range->Flags, highlight_border_color,
                                        style->FrameRounding, offset, range->From, range->To, bytes_per_line, i, line_base);
                                }

                                if (range->Flags & ImGuiHexEditorHighlightFlags_TextAutomaticContrast)
                                    byte_text_color = CalcContrastColor(range->Color);
                            }
                        }
                    }
                }

                ImDrawList_AddText_Vec2(draw_list, byte_bb.Min, byte_text_color, text, NULL);

                if (offset == select_start_byte)
                {
                    state->SelectCursorAnimationTime += io->DeltaTime;

                    if (!io->ConfigInputTextCursorBlink || fmodf(state->SelectCursorAnimationTime, 1.20f) <= 0.80f)
                    {
                        ImVec2 pos;
                        pos.x = byte_bb.Min.x;
                        pos.y = byte_bb.Max.y;

                        if (select_start_subbyte)
                            pos.x += char_size.x;
                        
                        ImDrawList_AddLine(draw_list, (ImVec2){ pos.x, pos.y }, (ImVec2){ pos.x + char_size.x, pos.y }, text_color, 1.0f);
                    }
                }

                const bool hovered = igItemHoverable(item_bb, id, ImGuiItemFlags_Inputable);

                if (select_drag_byte != -1 && offset == select_drag_byte && !mouse_left_down)
                {
                    next_select_drag_byte = -1;
                }
                else
                {
                    if (hovered)
                    {
                        const bool clicked = igIsItemClicked(ImGuiMouseButton_Left);

                        if (clicked)
                        {
                            next_select_start_byte = offset;
                            next_select_end_byte = offset;
                            next_select_drag_byte = offset;
                            next_select_drag_subbyte = mouse_pos.x > (byte_bb.Min.x + byte_bb.Max.x) * 0.5f;
                            next_select_start_subbyte = next_select_drag_subbyte;
                            next_last_selected_byte = offset;

                            igSetKeyboardFocusHere(0);
                        }
                        else if (mouse_left_down && select_drag_byte != -1)
                        {
                            if (offset >= select_drag_byte)
                            {
                                next_select_end_byte = offset;
                            }
                            else
                            {
                                next_select_start_byte = offset;
                                next_select_end_byte = select_drag_byte;
                                next_select_start_subbyte = 0;
                            }   

                            igSetKeyboardFocusHere(0);
                        }
                    }
                }

                if (offset == next_last_selected_byte && last_selected_byte != next_last_selected_byte)
                {
                    igSetKeyboardFocusHere(0);
                }

                if (offset == last_selected_byte && !state->ReadOnly && hex_key_pressed != ImGuiKey_None)
                {
                    IM_ASSERT(offset == select_start_byte || offset == select_end_byte);
                    const int subbyte = offset == select_start_byte ? select_start_subbyte : select_end_subbyte;

                    unsigned char wbyte;
                    if (subbyte)
                        wbyte = (byte & 0xf0) | KeyToHalfByte(hex_key_pressed);
                    else
                        wbyte = (KeyToHalfByte(hex_key_pressed) << 4) | (byte & 0x0f);

                    if (!state->WriteCallback)
                        *(unsigned char*)((char*)state->Bytes + n * bytes_per_line + i) = wbyte;
                    else
                        state->WriteCallback(state, n * bytes_per_line + i, &wbyte, sizeof(wbyte));

                    int* next_subbyte = (int*)(offset == select_start_byte ? &next_select_start_subbyte : &next_select_end_subbyte);
                    if (!subbyte)
                    {
                        next_select_start_byte = offset;
                        next_select_end_byte = offset;
                        *next_subbyte = 1;
                    }
                    else
                    {
                        next_last_selected_byte = offset + 1;
                        if (next_last_selected_byte >= state->MaxBytes - 1)
                            next_last_selected_byte = state->MaxBytes - 1;
                        else
                            *next_subbyte = 0;

                        next_select_start_byte = next_last_selected_byte;
                        next_select_end_byte = next_last_selected_byte;
                    }

                    state->SelectCursorAnimationTime = 0.f;
                }

                cursor.x += byte_size.x + spacing.x;
                if (i > 0 && state->Separators > 0 && (i + 1) % state->Separators == 0
                    && i != bytes_per_line - 1)
                    cursor.x += spacing.x;

                if (show_ascii)
                {
                    unsigned char b;
                    if (offset < state->MaxBytes)
                        b = line_buf[i];
                    else
                        b = 0x00;

                    bool has_ascii = HasAsciiRepresentation(b);

                    char txt[2];
                    txt[0] = has_ascii ? (char)b : '.';
                    txt[1] = '\0';

                    ImDrawList_AddText_Vec2(draw_list, byte_ascii, byte_text_color, txt, NULL);
                }

                igSetCursorScreenPos(cursor);
            }

            igNewLine();
            igGetCursorScreenPos(&cursor);
        }
    }
    ImGuiListClipper_End(&clipper);

    state->SelectStartByte = next_select_start_byte;
    state->SelectStartSubByte = next_select_start_subbyte;
    state->SelectEndByte = next_select_end_byte;
    state->SelectEndSubByte = next_select_end_subbyte;  
    state->LastSelectedByte = next_last_selected_byte;
    state->SelectDragByte = next_select_drag_byte;
    state->SelectDragSubByte = next_select_drag_subbyte;

    if (line_buf != stack_line_buf)
        igMemFree(line_buf);

    if (address_buf != stack_address_buf)
        igMemFree(address_buf);
    
    return true;
}

void igEndHexEditor(void)
{
    igEndChild();
}

bool igCalcHexEditorRowRange(int row_offset, int row_bytes_count, int range_min, int range_max, int* out_min, int* out_max)
{
    int abs_min;
    int abs_max;

    if (RangeRangeIntersection(row_offset, row_offset + row_bytes_count, range_min, range_max, &abs_min, &abs_max))
    {
        *out_min = abs_min - row_offset;
        *out_max = abs_max - row_offset;
        return true;
    }

    return false;
}