-- The ImGui bindings, bound to the frontend's own cimgui.
--
-- imgui/base.lua is generated at build time from this project's cimgui
-- metadata (see frontend/tools/gen_imgui_bindings.lua) and returns a loader
-- taking the name of the shared library to bind against. Requiring this module
-- gives every script the same instance, drawing into the context the frontend
-- already created -- nothing here makes a context or a window.

local ig = require("imgui.base")("cimgui")

-- ImGui's text functions take a printf format string, and the generated
-- bindings expose them as the raw variadic C functions. Called from Lua that is
-- a hazard with no upside:
--
--   * Any string built from emulator data can contain '%'. A memory viewer
--     scrolling onto the bytes "%s" sends printf through a garbage pointer and
--     "%n" has it write through one; either takes the whole process down, and
--     no pcall can catch it.
--   * The formatting is not usable anyway. LuaJIT passes a Lua number to a
--     variadic C function as a double, so "%d" reads nonsense. string.format
--     is the correct tool and is already right there.
--
-- So these are wrapped to pass the caller's string as an argument to a literal
-- "%s". Text is then rendered verbatim whatever it contains. Callers that
-- genuinely want C-side formatting can still reach ig.lib.igText and friends.
local UNSAFE_TEXT = {
	-- name              index of the format argument (1-based)
	Text                = 1,
	TextDisabled        = 1,
	TextWrapped         = 1,
	BulletText          = 1,
	SetTooltip          = 1,
	SetItemTooltip      = 1,
	TextColored         = 2, -- (col, fmt, ...)
	LabelText           = 2, -- (label, fmt, ...)
	TreeNode_StrStr     = 2, -- (str_id, fmt, ...)
	TreeNodeEx_StrStr   = 3, -- (str_id, flags, fmt, ...)
}

for name, position in pairs(UNSAFE_TEXT) do
	local raw = ig[name]
	if raw ~= nil then
		if position == 1 then
			ig[name] = function(text, ...)
				return raw("%s", tostring(text))
			end
		elseif position == 2 then
			ig[name] = function(a, text, ...)
				return raw(a, "%s", tostring(text))
			end
		else
			ig[name] = function(a, b, text, ...)
				return raw(a, b, "%s", tostring(text))
			end
		end
	end
end

return ig
