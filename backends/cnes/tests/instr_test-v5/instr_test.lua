local frames = 0

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
    -- set_a(0x00)
    -- set_x(0x00)
    -- set_y(0x00)
    -- set_pc(0xC000)
    -- set_sp(0xFD)
    -- set_status(0x24)
end

function onframe()
    frames = frames + 1

    local res = mem_read(0x6000)

    if res == 0x80 then
        return
    end

    -- Any non-zero result code means test failure.
    if (res ~= 0) then
        finish(string.format("InstrTest Failed: $6000=%02X", res))
        return
    end

    if res == 0 then
        --finish("InstrTest Succeeded.")
    else
        finish(string.format("InstrTest Failed: $6000=%02X", res))
    end
end
