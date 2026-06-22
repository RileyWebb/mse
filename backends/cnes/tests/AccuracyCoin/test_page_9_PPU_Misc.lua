local BUTTON_A = 0x01
local BUTTON_B = 0x02
local BUTTON_SELECT = 0x04
local BUTTON_START = 0x08
local BUTTON_UP = 0x10
local BUTTON_DOWN = 0x20
local BUTTON_LEFT = 0x40
local BUTTON_RIGHT = 0x80

local base_frame = 0
local last_seen_frame = -1
local done = false

local MOVE_CURSOR_TO_TOP_FRAMES = 12
local START_PRESS_FRAMES = 4
local WAIT_AFTER_START_FRAMES = 20 * 60
local CAPTURE_AFTER_START_FRAMES = 60 * 180
-- navigate pages
local PAGE_RIGHT_PRESS_FRAMES = 4
local WAIT_BETWEEN_PAGES = 10

-- We assume this script will be copied per test, and variables will be adjusted
local target_page_index = 9 -- 0-indexed page

local start_press_begin_frame = MOVE_CURSOR_TO_TOP_FRAMES + 1 + (target_page_index * (PAGE_RIGHT_PRESS_FRAMES + WAIT_BETWEEN_PAGES))
local start_press_end_frame = start_press_begin_frame + START_PRESS_FRAMES - 1
local capture_frame = start_press_end_frame + WAIT_AFTER_START_FRAMES + CAPTURE_AFTER_START_FRAMES

local function tile_to_char(tile)
        if tile >= 0x20 and tile <= 0x7E then
                return string.char(tile)
        end
        return "."
end

local function read_line_ascii(table_index, row)
        local chars = {}
        local row_base = row * 32
        for col = 0, 31 do
                local tile = ppu_read_nametable(table_index, row_base + col)
                chars[#chars + 1] = tile_to_char(tile)
        end
        return table.concat(chars)
end

local function read_line_hex(table_index, row)
        local parts = {}
        local row_base = row * 32
        for col = 0, 31 do
                local tile = ppu_read_nametable(table_index, row_base + col)
                parts[#parts + 1] = string.format("%02X", tile)
        end
        return table.concat(parts, " ")
end

local RESULT_START_ROW = 8
local RESULT_START_COL = 6
local RESULT_COLS = 26
local RESULT_COUNT = 138

local fail_descriptions = {{

}}

local function decode_result_value(value)
        if value >= 0x01 and value <= 0x09 then
                return "IGN"
        end

        if value == 0x0A or value == 0x0E or value == 0x10 or value == 0x19 then
                return "IGN"
        end

        if value == 0x24 then
                return "IGN"
        end

        if value >= 0xD0 and value <= 0xD5 then
                return "IGN"
        end

        if value == 0xFE then
                return "PASS"
        end

        if math.floor(value / 16) == 0x4 then
                local code = string.format("%X", value % 16)
                if fail_descriptions[code] then
                    return string.format("FAIL %s - %s", code, fail_descriptions[code])
                else
                    return string.format("FAIL %s (Unknown failure)", code)
                end
        end

        return string.format("UNKNOWN %02X", value)
end

local function read_result_cell(result_index)
        local row_offset = math.floor(result_index / RESULT_COLS)
        local col_offset = result_index % RESULT_COLS
        return ppu_read_nametable(0, (RESULT_START_ROW + row_offset) * 32 + (RESULT_START_COL + col_offset))
end

local function read_full_framebuffer_checksum()
        local sum = 0
        for y = 0, 239 do
                for x = 0, 255 do
                        local px = ppu_read_pixel(x, y)
                        sum = (sum + px) % 4294967296
                end
        end
        return sum
end

local function results_ready()
        local marker_count = 0

        for index = 0, RESULT_COUNT - 1 do
                local value = read_result_cell(index)
                if value == 0xFE or math.floor(value / 16) == 0x4 then
                        marker_count = marker_count + 1
                end
        end

        return marker_count > 0
end

local function dump_results_from_tile_buffers()
        print("AccuracyCoin tile buffer snapshot begin - Page 9: PPU Misc.")
        local framebuffer_sum = read_full_framebuffer_checksum()
        print(string.format("Framebuffer checksum: %08X", framebuffer_sum))

        local pass_count = 0
        local fail_count = 0
        local ignored_count = 0
        local unknown_count = 0

        for index = 0, RESULT_COUNT - 1 do
                local value = read_result_cell(index)
                local decoded = decode_result_value(value)
                if decoded == "PASS" then
                        pass_count = pass_count + 1
                elseif string.sub(decoded, 1, 4) == "FAIL" then
                        fail_count = fail_count + 1
                elseif decoded == "IGN" then
                        ignored_count = ignored_count + 1
                else
                        unknown_count = unknown_count + 1
                end

                if decoded ~= "IGN" then
                        print(string.format("Result %03d: %s (tile=%02X)", index + 1, decoded, value))
                end
        end

        print(string.format("AccuracyCoin summary: pass=%d fail=%d ignored=%d unknown=%d", pass_count, fail_count, ignored_count, unknown_count))

        if fail_count == 0 and unknown_count == 0 then
                exit_with_code(0)
        else
                exit_with_code(1)
        end
end

function onstart()
        base_frame = get_frame_count()
        last_seen_frame = base_frame - 1
        done = false
        set_controller_state(0, 0)
end

function onframe()
        if done then
                return
        end

        local current_frame = get_frame_count()
        if current_frame == last_seen_frame then
                return
        end
        last_seen_frame = current_frame

        local elapsed_frames = current_frame - base_frame

        if elapsed_frames <= MOVE_CURSOR_TO_TOP_FRAMES then
                set_controller_button(0, BUTTON_UP, true)
        elseif elapsed_frames == MOVE_CURSOR_TO_TOP_FRAMES + 1 then
                set_controller_button(0, BUTTON_UP, false)
        end
        
        local current_page_index = math.floor((elapsed_frames - (MOVE_CURSOR_TO_TOP_FRAMES + 1)) / (PAGE_RIGHT_PRESS_FRAMES + WAIT_BETWEEN_PAGES))
        local relative_frame = (elapsed_frames - (MOVE_CURSOR_TO_TOP_FRAMES + 1)) % (PAGE_RIGHT_PRESS_FRAMES + WAIT_BETWEEN_PAGES)
        
        if current_page_index >= 0 and current_page_index < target_page_index then
            if relative_frame < PAGE_RIGHT_PRESS_FRAMES then
                set_controller_button(0, BUTTON_RIGHT, true)
            else
                set_controller_button(0, BUTTON_RIGHT, false)
            end
        end

        if elapsed_frames >= start_press_begin_frame and elapsed_frames <= start_press_end_frame then
                set_controller_button(0, BUTTON_START, true)
        elseif elapsed_frames == start_press_end_frame + 1 then
                set_controller_button(0, BUTTON_START, false)
        end

        if elapsed_frames >= capture_frame and not done then
                dump_results_from_tile_buffers()
                done = true
        end
end
