-- Generates Lua FFI bindings for this project's own cimgui build.
--
-- LuaJIT-ImGui ships pre-generated bindings, but they are generated against its
-- pinned cimgui. Struct layouts and enum values have to match the DLL exactly
-- or reads silently land on the wrong fields, so the bindings are generated
-- here instead, from the metadata our cimgui emits into generator/output.
--
-- The heavy lifting is LuaJIT-ImGui's class_gen.lua (MIT, Victor Bombi), which
-- turns that metadata into ergonomic wrappers with default arguments and
-- overload dispatch. This drives it in place of their generator.lua, which
-- assumes their repository layout and their extra cimgui modules.
--
-- Usage:
--   luajit gen_imgui_bindings.lua <workdir> <cimgui_src> <ljimgui_lua> <prelude> <out_dir> [cc]
--
--   workdir      staging directory; class_gen.lua resolves paths relative to it
--   cimgui_src   the real cimgui source tree (for its headers and metadata)
--   ljimgui_lua  LuaJIT-ImGui's lua/ directory, where class_gen.lua lives
--   prelude      imgui_prelude.lua, prepended to the generated wrappers
--   out_dir      where imgui/cdefs.lua and imgui/base.lua are written
--   cc           C compiler to preprocess the headers with (default: gcc)

local workdir     = assert(arg[1], "workdir required")
local cimgui_src  = assert(arg[2], "cimgui source dir required")
local ljimgui_lua = assert(arg[3], "LuaJIT-ImGui lua dir required")
local prelude     = assert(arg[4], "prelude path required")
local out_dir     = assert(arg[5], "output dir required")
local cc          = arg[6] or "gcc"

-- class_gen.lua is required from the staging directory, and cpp2ffi from the
-- cimgui copy inside it; both resolve by relative path, so the process has to
-- run with workdir/lua as its working directory.
package.path = table.concat({
	workdir .. "/lua/?.lua",
	workdir .. "/cimgui/generator/?.lua",
	package.path,
}, ";")

local cpp2ffi   = require("cpp2ffi")
local save_data = cpp2ffi.save_data
local read_data = cpp2ffi.read_data
local location  = cpp2ffi.location

local function log(...)
	io.write("[imgui-bindings] ", table.concat({ ... }, " "), "\n")
end

-- Preprocesses a header and keeps only the declarations that came from it,
-- dropping the dllexport decoration LuaJIT's parser will not accept.
local function get_cdefs(command, which, cdef)
	cdef = cdef or {}

	local pipe, err = io.popen(command, "r")
	if not pipe then
		error("could not run the preprocessor: " .. tostring(err))
	end

	for line in location(pipe, { which }) do
		line = line:gsub("extern __attribute__%(%(dllexport%)%)%s*", "")
		line = line:gsub("extern __declspec%(dllexport%)%s*", "")
		line = line:gsub("__declspec%(dllimport%)%s*", "")
		if line ~= "" then
			-- static const of a non-integer type is a definition, not a
			-- declaration, and ffi.cdef rejects it.
			local skip = line:match("^%s*static const") and not line:match("static const int")
			if not skip then
				table.insert(cdef, line)
			end
		end
	end
	pipe:close()

	return cdef
end

-- 1. cdefs, straight from the headers this project actually compiles.
--
-- IMGUI_USE_WCHAR32 matters: it changes the size of ImWchar and therefore the
-- layout of every font struct. It has to match frontend/cmake/cimgui.cmake.
log("preprocessing cimgui.h")

local defines = table.concat({
	"-DCIMGUI_DEFINE_ENUMS_AND_STRUCTS",
	"-DIMGUI_USE_WCHAR32",
	"-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS=1",
}, " ")

local command = string.format([[%s -E %s -I "%s" -I "%s" "%s/cimgui.h"]],
	cc, defines, cimgui_src, cimgui_src .. "/imgui", cimgui_src)

local cdefs = get_cdefs(command, "cimgui")

if #cdefs < 500 then
	error(string.format("only %d cdef lines from cimgui.h - the preprocessor call failed?", #cdefs))
end

log(string.format("got %d declarations", #cdefs))

table.insert(cdefs, 1, "typedef void FILE;")
table.insert(cdefs, 1, 'local ffi = require"ffi"\nlocal cdecl = ""\ncdecl = cdecl .. [[')
table.insert(cdefs, "]]\nreturn cdecl\n")

-- class_gen.lua reads ./imgui/cdefs.lua relative to the working directory, so
-- it has to land in the staging tree before the wrappers are generated.
os.execute(string.format('mkdir "%s" 2>nul', (workdir .. "/lua/imgui"):gsub("/", "\\")))
save_data(workdir .. "/lua/imgui/cdefs.lua", table.concat(cdefs, "\n"))
log("wrote cdefs.lua")

-- 2. Wrappers, from the same metadata the C bindings were generated from.
--
-- class_gen.lua has a stray top-level `require"anima.utils"`, a debugging
-- dependency from its author's own project that is not part of LuaJIT-ImGui and
-- is only used by commented-out prtable() calls. Stage a copy with that line
-- neutralised rather than vendoring a fork we would have to re-merge.
-- Newer cimgui records call_args as the call its own C++ shim makes, with the
-- arguments wrapped in conversions -- "(ConvertToCPP_ImVec4(col))",
-- "(reinterpret_cast<const ImVec2>(points))" -- and keeps the plain argument
-- list in call_args_old. class_gen.lua predates that and emits call_args
-- straight into a Lua parameter list, where the conversions are syntax errors.
--
-- Wherever the two differ, the difference is exactly that C++-side conversion,
-- and FFI calls the C entry point directly, so the plain form is what we want.
log("normalising definitions metadata")

local defs_path = workdir .. "/cimgui/generator/output/definitions.lua"
local defs_text = read_data(defs_path)
local folded = 0

defs_text = defs_text:gsub('(call_args=")([^"]*)(",%s*\n%s*call_args_old=")([^"]*)(",)',
	function(head, call_args, mid, call_args_old, tail)
		if call_args ~= call_args_old then
			folded = folded + 1
			return head .. call_args_old .. mid .. call_args_old .. tail
		end
		return nil -- leave untouched
	end)

if folded > 0 then
	save_data(defs_path, defs_text)
end
log(string.format("folded %d C++-conversion call sites", folded))

log("staging class_gen.lua")

-- The replacement must contain no long-bracket delimiter: one of the two
-- occurrences sits inside a --[[ tests ]] block, and a "]]" in the replacement
-- would close that comment early and un-comment the dead code after it.
local class_gen_src = read_data(ljimgui_lua .. "/class_gen.lua")
local patched, replaced = class_gen_src:gsub('require"anima%.utils"',
	'local function prtable() end -- anima.utils is debug-only and not shipped')

if replaced == 0 and not class_gen_src:find("prtable", 1, true) then
	error("class_gen.lua no longer looks like the expected upstream file")
end

save_data(workdir .. "/lua/class_gen.lua", patched)

log("generating wrappers")

local class_gen = require("class_gen")
local wrappers  = class_gen({ "cimgui" }, false)

-- 3. Stitch prelude and wrappers into a loader the frontend can require.
local body = table.concat({
	"return function(cimguimodule) --loader\n",
	read_data(prelude),
	"\n",
	wrappers,
	"\nend --loader\n",
})

save_data(out_dir .. "/imgui/base.lua", body)
save_data(out_dir .. "/imgui/cdefs.lua", read_data(workdir .. "/lua/imgui/cdefs.lua"))

log("wrote " .. out_dir .. "/imgui/base.lua")
log("done")
