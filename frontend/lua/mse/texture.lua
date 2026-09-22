-- CPU pixel buffers as ImGui textures.
--
-- ImGui 1.92 lets a caller hand it an ImTextureData holding RGBA pixels and
-- have the renderer backend create and update the GPU texture for it. That is
-- what makes this possible from Lua at all: panels never see SDL_GPU, and the
-- frontend needs no per-panel texture code.
--
-- The alternative -- one ImDrawList rectangle per pixel -- does not survive
-- contact with anything real. A 128x128 pattern table is 16384 quads, or 65536
-- vertices, and ImGui's default 16-bit vertex indices overflow at exactly that
-- point.
--
-- Textures registered here are never destroyed. A debug panel allocates a
-- handful at fixed sizes and reuses them for the life of the process, so the
-- bookkeeping to free them would cost more than it saves.

local ffi = require("ffi")
local ig  = require("mse.imgui")

-- Not in the generated bindings: cimgui exposes these two on the internal API,
-- past where the generator stops. They are ordinary exports of cimgui.dll and
-- are declared in its cdefs, so ig.lib reaches them.
local lib = ig.lib

local M = {}

local Texture = {}
Texture.__index = Texture

--- Creates a width x height RGBA texture. Pixels start undefined; call
--- upload() before drawing it.
function M.new(width, height)
	local self = setmetatable({
		width  = width,
		height = height,
		bytes  = width * height * 4,
		data   = nil,
	}, Texture)

	self:_create()
	return self
end

function Texture:_create()
	-- Deliberately the raw constructor rather than ig.ImTextureData(), which
	-- attaches a __gc that would destroy this out from under ImGui: the
	-- register call below hands ownership over for good.
	local data = lib.ImTextureData_ImTextureData()
	lib.ImTextureData_Create(data, lib.ImTextureFormat_RGBA32, self.width, self.height)
	lib.igRegisterUserTexture(data)

	self.data = data
end

--- Copies width*height*4 bytes of RGBA from `source` into the texture and
--- marks it for upload. `source` is anything ffi.copy accepts as a pointer.
function Texture:upload(source)
	local data = self.data

	-- ImGui destroys a texture whose backend was torn down and restarted. It
	-- is not expected here, but recreating is cheap and beats drawing through
	-- a dead handle.
	if data.Status == lib.ImTextureStatus_Destroyed then
		self:_create()
		data = self.data
	end

	ffi.copy(lib.ImTextureData_GetPixels(data), source, self.bytes)

	-- A texture waiting to be created is already going to upload in full;
	-- saying "updates" instead would lose the create.
	if data.Status == lib.ImTextureStatus_OK then
		-- The SDL_GPU backend uploads UpdateRect for an update, so it has to
		-- cover everything that just changed.
		data.UpdateRect.x = 0
		data.UpdateRect.y = 0
		data.UpdateRect.w = self.width
		data.UpdateRect.h = self.height
		lib.ImTextureData_SetStatus(data, lib.ImTextureStatus_WantUpdates)
	end
end

--- The ImTextureRef to pass to ig.Image and friends.
function Texture:ref()
	return lib.ImTextureData_GetTexRef(self.data)
end

--- True once the backend has actually uploaded it. Drawing before this is
--- harmless -- ImGui skips the draw -- but a panel may want to say so.
function Texture:ready()
	return self.data.Status == lib.ImTextureStatus_OK
end

return M
