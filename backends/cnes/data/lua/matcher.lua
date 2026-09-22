local xml2lua = require("xml2lua")
local handler = require("xmlhandler.tree")
local md5 = require("md5")
local http = require("socket.http")

local db_cache = nil

local function ensure_array(t)
    if not t then return {} end
    if t[1] == nil and next(t) ~= nil then return { t } end
    return t
end

-- Proper URL encoding for Libretro Thumbnail paths
local function url_encode(str)
    if not str then return "" end
    -- Libretro replaces some characters like '&' with '_'
    str = str:gsub("&", "_")
    str = str:gsub("([^%w %-%_%.%~])", function(c)
        return string.format("%%%02X", string.byte(c))
    end)
    -- Spaces in URLs are conventionally %20
    str = str:gsub(" ", "%%20")
    return str
end

local function load_db()
    if db_cache then 
        print("[DEBUG] Using cached XML database.")
        return db_cache 
    end

    local filename = "cnes/data/nes_db.xml"
    print("[DEBUG] Loading XML database from: " .. filename)
    local file, err = io.open(filename, "r")
    if not file then
        print("[DEBUG] ERROR: Could not open XML file: " .. tostring(err))
        return {}
    end
    local xml_content = file:read("*all")
    file:close()
    print("[DEBUG] XML file read successful. Size: " .. #xml_content .. " bytes. Parsing...")

    local parser = xml2lua.parser(handler)
    parser:parse(xml_content)

    -- Navigate directly to the No-Intro datafile structure
    local datafile = handler.root and handler.root.datafile
    if not datafile and handler.root then
        for k, v in pairs(handler.root) do
            if type(v) == "table" and v.game then
                datafile = v
                break
            end
        end
    end

    db_cache = ensure_array(datafile and datafile.game)
    print("[DEBUG] XML parsing complete. Found " .. #db_cache .. " game entries in database.")
    return db_cache
end

function match_rom(rom_path)
    print("\n[DEBUG] Starting ROM match for path: " .. tostring(rom_path))
    local games = load_db()
    local matched_game = nil

    -- 1. Read file, compute MD5, and extract Header Bytes
    local f = io.open(rom_path, "rb")
    local file_md5 = ""
    local file_header_hex = ""
    if f then
        local content = f:read("*a")
        f:close()
        
        -- Compute MD5
        file_md5 = string.lower(md5.sumhexa(content))
        print("[DEBUG] Computed ROM MD5: " .. file_md5)
        
        -- Convert the first 16 bytes to space-separated hex values
        local header_bytes = string.sub(content, 1, 16)
        local hex_table = {}
        for i = 1, #header_bytes do
            table.insert(hex_table, string.format("%02X", string.byte(header_bytes, i)))
        end
        file_header_hex = table.concat(hex_table, " ")
        print("[DEBUG] Extracted File Header: " .. file_header_hex)
    else
        print("[DEBUG] WARNING: Could not open ROM file to calculate hashes: " .. tostring(rom_path))
    end

    -- 2. Pass 1: Try to match by basic filename matching (Without extension) first
    local base_name = rom_path:match("([^/\\]+)%.[^%.]+$") or rom_path:match("([^/\\]+)$")
    local lower_base_name = string.lower(base_name)
    print("[DEBUG] Pass 1: Attempting match by filename using: '" .. lower_base_name .. "'")
    
    for _, game in ipairs(games) do
        if game._attr and game._attr.name and string.lower(game._attr.name) == lower_base_name then
            matched_game = game
            print("[DEBUG] Match Found via Filename! Game: " .. tostring(game._attr.name))
            
            -- Validation: Check MD5 and Header Bytes against data structure
            local roms = ensure_array(game.rom)
            for _, rom_node in ipairs(roms) do
                if rom_node._attr then
                    -- Validate MD5
                    if rom_node._attr.md5 and file_md5 ~= "" then
                        local expected_md5 = string.lower(rom_node._attr.md5)
                        if expected_md5 ~= file_md5 then
                            print("[WARNING] MD5 mismatch for '" .. game._attr.name .. "'! Expected: " .. expected_md5 .. ", Got: " .. file_md5)
                        else
                            print("[DEBUG] Verification: MD5 checks out successfully.")
                        end
                    end
                    -- Validate Header Bytes
                    if rom_node._attr.header and file_header_hex ~= "" then
                        local expected_header = string.upper(rom_node._attr.header)
                        if expected_header ~= string.upper(file_header_hex) then
                            print("[WARNING] Header mismatch for '" .. game._attr.name .. "'! Expected: " .. expected_header .. ", Got: " .. file_header_hex)
                        else
                            print("[DEBUG] Verification: Header bytes check out successfully.")
                        end
                    end
                end
            end
            break
        end
    end

    -- 3. Pass 2: Fallback to MD5 matching if name match failed
    if not matched_game and file_md5 ~= "" then
        print("[DEBUG] Pass 1 Failed. Pass 2: Attempting fallback match by MD5 hash...")
        for _, game in ipairs(games) do
            local roms = ensure_array(game.rom)
            for _, rom_node in ipairs(roms) do
                if rom_node._attr and rom_node._attr.md5 then
                    if string.lower(rom_node._attr.md5) == file_md5 then
                        matched_game = game
                        print("[DEBUG] Match Found via MD5 Fallback! Game: " .. tostring(game._attr and game._attr.name))
                        break
                    end
                end
            end
            if matched_game then break end
        end
    end

    if not matched_game then 
        print("[DEBUG] CRITICAL: No match found in DB for " .. tostring(rom_path))
        return nil 
    end

    -- Extract metadata
    local game_name = matched_game._attr and matched_game._attr.name or "Unknown Game"
    local release_year = 0 

    -- 4. Download Artwork (Commented out for now)
    local artwork_data = nil
    --[[
    local safe_name = url_encode(game_name)
    local url = "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Nintendo_Entertainment_System/master/Named_Boxarts/" .. safe_name .. ".png"
    
    print("[DEBUG] Encoded Safe Name: '" .. safe_name .. "'")
    print("[DEBUG] Attempting to download Boxart from: " .. url)
    
    local body, code = http.request(url)
    if code == 200 and body then
        print("[DEBUG] Success! Downloaded " .. tostring(#body) .. " bytes of artwork.")
        artwork_data = body
    else
        print("[DEBUG] ERROR: Failed to download artwork. HTTP Code: " .. tostring(code))
    end
    --]]

    print("[DEBUG] Process complete for: " .. game_name .. "\n")
    return game_name, release_year, artwork_data
end