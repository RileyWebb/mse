#ifndef CIMGUIMPL_HEX_H
#define CIMGUIMPL_HEX_H

#include "cimgui.h"
#include <stdbool.h>
#include <string.h>

typedef int ImGuiHexEditorHighlightFlags;
enum ImGuiHexEditorHighlightFlags_
{
    ImGuiHexEditorHighlightFlags_None = 0,
    ImGuiHexEditorHighlightFlags_Apply = 1 << 0,
    ImGuiHexEditorHighlightFlags_TextAutomaticContrast = 1 << 1,
    ImGuiHexEditorHighlightFlags_FullSized = 1 << 2, // Highlight entire byte space including it's container, has no effect on ascii
    ImGuiHexEditorHighlightFlags_Ascii = 1 << 3, // Highlight ascii (doesn't affect single byte highlighting)
    ImGuiHexEditorHighlightFlags_Border = 1 << 4,
    ImGuiHexEditorHighlightFlags_OverrideBorderColor = 1 << 5,
    ImGuiHexEditorHighlightFlags_BorderAutomaticContrast = 1 << 6,
};

typedef struct ImGuiHexEditorHighlightRange ImGuiHexEditorHighlightRange;
struct ImGuiHexEditorHighlightRange
{
    int From;
    int To;
    ImU32 Color;
    ImU32 BorderColor;
    ImGuiHexEditorHighlightFlags Flags;
};

// Define a vector for our custom highlight range type
typedef struct ImVector_ImGuiHexEditorHighlightRange {
    int Size;
    int Capacity;
    ImGuiHexEditorHighlightRange* Data;
} ImVector_ImGuiHexEditorHighlightRange;

typedef int ImGuiHexEditorClipboardFlags;
enum ImGuiHexEditorClipboardFlags_
{
    ImGuiHexEditorClipboardFlags_None = 0,
    ImGuiHexEditorClipboardFlags_Multiline = 1 << 0, // Separate resulting hex editor lines with carriage return
};

typedef struct ImGuiHexEditorState ImGuiHexEditorState;
struct ImGuiHexEditorState
{
    void* Bytes;
    int MaxBytes;
    int BytesPerLine;
    bool ShowPrintable;
    bool LowercaseBytes;
    bool RenderZeroesDisabled;
    bool ShowAddress;
    int AddressChars;
    bool ShowAscii;
    bool ReadOnly;
    int Separators;
    void* UserData;
    ImVector_ImGuiHexEditorHighlightRange HighlightRanges;
    bool EnableClipboard;
    ImGuiHexEditorClipboardFlags ClipboardFlags;

    int(*ReadCallback)(ImGuiHexEditorState* state, int offset, void* buf, int size);
    int(*WriteCallback)(ImGuiHexEditorState* state, int offset, void* buf, int size);
    bool(*GetAddressNameCallback)(ImGuiHexEditorState* state, int offset, char* buf, int size);
    ImGuiHexEditorHighlightFlags(*SingleHighlightCallback)(ImGuiHexEditorState* state, int offset, ImU32* color, ImU32* text_color, ImU32* border_color);
    void(*HighlightRangesCallback)(ImGuiHexEditorState* state, int display_start, int display_end);

    int SelectStartByte;
    int SelectStartSubByte;
    int SelectEndByte;
    int SelectEndSubByte;
    int LastSelectedByte;
    int SelectDragByte;
    int SelectDragSubByte;
    float SelectCursorAnimationTime;

    ImGuiHexEditorHighlightFlags SelectionHighlightFlags;
};

// C does not support default struct initializers. Call this on your state object after creation!
static inline void ImGuiHexEditorState_Init(ImGuiHexEditorState* state)
{
    memset(state, 0, sizeof(ImGuiHexEditorState));
    state->BytesPerLine = -1;
    state->ShowPrintable = false;
    state->LowercaseBytes = false;
    state->RenderZeroesDisabled = true;
    state->ShowAddress = true;
    state->AddressChars = -1;
    state->ShowAscii = true;
    state->ReadOnly = false;
    state->Separators = 8;
    state->EnableClipboard = true;
    state->ClipboardFlags = ImGuiHexEditorClipboardFlags_Multiline;
    
    state->SelectStartByte = -1;
    state->SelectStartSubByte = 0;
    state->SelectEndByte = -1;
    state->SelectEndSubByte = 0;
    state->LastSelectedByte = -1;
    state->SelectDragByte = -1;
    state->SelectDragSubByte = 0;
    state->SelectCursorAnimationTime = 0.f;

    state->SelectionHighlightFlags = ImGuiHexEditorHighlightFlags_FullSized | ImGuiHexEditorHighlightFlags_Ascii;
}

// Main Editor Output
bool igBeginHexEditor(const char* str_id, ImGuiHexEditorState* state, ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags);
void igEndHexEditor(void);

// Helpers
bool igCalcHexEditorRowRange(int row_offset, int row_bytes_count, int range_min, int range_max, int* out_min, int* out_max);

#endif // CIMGUIMPL_HEX_H