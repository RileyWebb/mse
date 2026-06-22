local frames = 0
local max_frames = 1800
local warmup_frames = 10

local function finish(message)
    if message then
        print(message)
    end
    -- Always exit with code 0; CTest verifies pass/fail by output regex.
    set_exit_code(0)
    exit()
end

function onstart()
    -- Match legacy cpu_tests.c initialization for nestest automation mode.
    set_a(0x00)
    set_x(0x00)
    set_y(0x00)
    set_pc(0xC000)
    set_sp(0xFD)
    set_status(0x24)
end

function onframe()
    frames = frames + 1

    local res_02 = mem_read(0x0002)
    local res_03 = mem_read(0x0003)

    -- Any non-zero result code means test failure.
    if frames > warmup_frames and (res_02 ~= 0 or res_03 ~= 0) then
        finish(string.format("Nestest Failed: $0002=%02X, $0003=%02X", res_02, res_03))
        return
    end

    -- After bounded runtime, treat zero/zero as pass (legacy harness behavior).
    if frames >= max_frames then
        if res_02 == 0 and res_03 == 0 then
            finish("Nestest Succeeded.")
        else
            finish(string.format("Nestest Failed: $0002=%02X, $0003=%02X", res_02, res_03))
        end
    end
end
