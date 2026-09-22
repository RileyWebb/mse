-- Prelude for the generated ImGui bindings.
--
-- The generator concatenates this with the wrapper code that LuaJIT-ImGui's
-- class_gen.lua produces from our own cimgui's metadata, so everything below is
-- the set of locals that generated code expects to find: ffi, lib, M, ImVec2
-- and ImVec4.
--
-- This stands in for LuaJIT-ImGui's own imgui_base.lua, which assumes their
-- extended cimgui fork (Log_new, the ImGuiZMO quaternion helpers) that this
-- project's cimgui does not build. The ImVec2/ImVec4 metatypes are adapted from
-- that file, MIT licensed, Copyright (c) 2017-2019 Victor Bombi.
--
-- Nothing here creates a context or a window: the frontend owns those, and Lua
-- draws into the frame it is already inside.

local ffi = require("ffi")
local cdecl = require("imgui.cdefs")

assert(cdecl, "imgui.cdefs missing - the bindings were not generated")
ffi.cdef(cdecl)

local lib = ffi.load(cimguimodule)

----------- ImVec2
local ImVec2
ImVec2 = {
	__add = function(a, b) return ImVec2(a.x + b.x, a.y + b.y) end,
	__sub = function(a, b) return ImVec2(a.x - b.x, a.y - b.y) end,
	__unm = function(a) return ImVec2(-a.x, -a.y) end,
	__mul = function(a, b)
		if not ffi.istype(ImVec2, a) then a, b = b, a end
		if not ffi.istype(ImVec2, b) then
			assert(type(b) == "number", "ImVec2 multiplied by neither number nor ImVec2")
			return ImVec2(a.x * b, a.y * b)
		end
		return ImVec2(a.x * b.x, a.y * b.y)
	end,
	__div = function(a, b)
		if not ffi.istype(ImVec2, b) then
			assert(type(b) == "number", "ImVec2 divided by neither number nor ImVec2")
			return ImVec2(a.x / b, a.y / b)
		end
		return ImVec2(a.x / b.x, a.y / b.y)
	end,
	__eq = function(a, b)
		if (not ffi.istype(ImVec2, b)) or (not ffi.istype(ImVec2, a)) then return false end
		return a.x == b.x and a.y == b.y
	end,
	__len = function(a) return math.sqrt(a.x * a.x + a.y * a.y) end,
	norm = function(a) return math.sqrt(a.x * a.x + a.y * a.y) end,
	__tostring = function(v) return "ImVec2<" .. v.x .. "," .. v.y .. ">" end,
}
ImVec2.__index = ImVec2
ImVec2 = ffi.metatype("ImVec2", ImVec2)

----------- ImVec4
local ImVec4 = {}
ImVec4.__index = ImVec4
ImVec4.__tostring = function(v)
	return "ImVec4<" .. v.x .. "," .. v.y .. "," .. v.z .. "," .. v.w .. ">"
end
ImVec4 = ffi.metatype("ImVec4", ImVec4)

local M = { ImVec2 = ImVec2, ImVec4 = ImVec4, lib = lib, ffi = ffi }

M.FLT_MAX = lib.igGET_FLT_MAX()
M.FLT_MIN = lib.igGET_FLT_MIN()

function M.U32(r, g, b, a)
	return lib.igGetColorU32_Vec4(ImVec4(r, g, b, a or 1))
end

-- Scratch cells for the out-parameters ImGui takes by pointer, so callers can
-- write `if ig.Checkbox("x", ref) then` without allocating per frame.
function M.bool(value)
	return ffi.new("bool[1]", value and true or false)
end

function M.int(value)
	return ffi.new("int[1]", value or 0)
end

function M.float(value)
	return ffi.new("float[1]", value or 0)
end

-- A resizable text buffer for InputText.
function M.buffer(size, initial)
	local buf = ffi.new("char[?]", size)
	if initial then
		local n = math.min(#initial, size - 1)
		ffi.copy(buf, initial, n)
		buf[n] = 0
	end
	return buf
end

function M.str(buf)
	return ffi.string(buf)
end
