# cNES Lua Scripting API

## Overview
cNES supports Lua scripting for automation, debugging, and ROM testing. Scripts are loaded at startup and called once per frame.

## Usage
```bash
cNES --script <script.lua> [rom.nes]
cNES example.lua game.nes
```

## API Functions

### Memory Access
- `mem_read(address)` - Read byte from memory (0x0000-0xFFFF)
  - Returns: 8-bit value (0-255)
  
- `mem_write(address, value)` - Write byte to memory
  - address: 0x0000-0xFFFF
  - value: 0-255
  
- `mem_read16(address)` - Read 16-bit little-endian word from memory
  - Returns: 16-bit value

### CPU Registers
- `get_pc()` - Get Program Counter
  - Returns: 16-bit value
  
- `get_a()` - Get Accumulator register
  - Returns: 8-bit value
  
- `get_x()` - Get X register
  - Returns: 8-bit value
  
- `get_y()` - Get Y register
  - Returns: 8-bit value

### Control
- `exit()` - Exit the emulator
  - Stops the emulation loop and closes the application

## Callback Functions

### onframe()
Called once per frame during emulation. This is where you put your per-frame logic.

```lua
function onframe()
    local pc = get_pc()
    if pc == 0x8000 then
        print("Breakpoint at 0x8000")
    end
end
```

## Examples

### Simple Tracer
```lua
function onframe()
    print(string.format("PC: 0x%04X", get_pc()))
end
```

### Memory Monitor
```lua
function onframe()
    local val = mem_read(0x0100)
    if val > 0x80 then
        print("Stack pointer high!")
    end
end
```

### ROM Automation
```lua
local frames = 0

function onframe()
    frames = frames + 1
    if frames > 1800 then  -- 30 seconds at 60 FPS
        exit()
    end
end
```

## Implementation Notes

- Lua is optional - the emulator compiles without it
- Enable LuaJIT with: `-DUSE_LUAJIT=ON` during CMake configuration
- Script loading happens after ROM load during startup
- If both ROM and script are provided on command line, ROM loads first
- The `mem_read/write` functions access CPU address space ($0000-$FFFF)

## Performance Considerations

- The `onframe()` function is called 16,800 times per second (60 FPS * 280 cycles per frame)
- Keep Lua calculations lightweight to avoid frame rate drops
- Use local variables whenever possible for better performance
