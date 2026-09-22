-- Panel registry for backend-supplied debug UI.
--
-- Backends ship Lua scripts that register panels here; the frontend calls
-- draw() once per ImGui frame and menu() inside its View menu. Scripts run on
-- the UI thread, inside the frame that is already open -- ImGui is
-- single-threaded and everything here happens between NewFrame and Render.
--
-- A panel that errors must not take the frame down with it. Each draw is
-- pcall'd between ImGui's own ErrorRecoveryStoreState and
-- ErrorRecoveryTryToRecoverState, which unwinds any windows, tables or stack
-- pushes the script left open. A panel that throws is reported once and then
-- disabled, so a broken script does not spam the log every frame.

local ig = require("mse.imgui")

local ui = {}

local panels = {}
local by_id  = {}

--- Registers a panel.
--
--   ui.panel{
--     id      = "cnes.cpu",        -- unique; also the menu/window identity
--     title   = "cNES CPU",        -- window title
--     group   = "cNES",            -- menu grouping, optional
--     open    = false,             -- initial visibility
--     flags   = 0,                 -- ImGuiWindowFlags, optional
--     draw    = function() ... end -- contents; the host owns Begin/End
--   }
function ui.panel(def)
	assert(type(def) == "table", "ui.panel expects a table")
	assert(type(def.id) == "string", "ui.panel: id must be a string")
	assert(type(def.draw) == "function", "ui.panel: draw must be a function")

	if by_id[def.id] then
		-- Re-registering replaces the old definition, so a script can be
		-- reloaded without restarting the frontend.
		local existing = by_id[def.id]
		existing.title = def.title or existing.title
		existing.group = def.group or existing.group
		existing.flags = def.flags or existing.flags
		existing.draw  = def.draw
		existing.failed = nil
		return existing
	end

	local panel = {
		id    = def.id,
		title = def.title or def.id,
		group = def.group or "Debug",
		flags = def.flags or 0,
		draw  = def.draw,
		-- ImGui wants a bool* it can clear when the close button is used.
		open  = ig.bool(def.open and true or false),
		-- Allocated once; reused every frame rather than churning the GC.
		recovery = ig.ffi.new("ImGuiErrorRecoveryState"),
	}

	panels[#panels + 1] = panel
	by_id[def.id] = panel
	return panel
end

--- Puts a directory on package.path, once.
--
-- Backend scripts are run by path, not required, so their own directory is not
-- searchable when they require a sibling. The host calls this for each script's
-- directory and its parent before running it; without the duplicate check,
-- package.path would grow by two entries per script per load.
function ui.add_path(dir)
	if type(dir) ~= "string" or dir == "" then
		return
	end

	local entry = dir .. "/?.lua"
	if not package.path:find(entry, 1, true) then
		package.path = package.path .. ";" .. entry
	end
end

function ui.get(id)
	return by_id[id]
end

function ui.show(id, visible)
	local panel = by_id[id]
	if panel then
		panel.open[0] = visible ~= false
	end
end

function ui.count()
	return #panels
end

-- Menu entries, grouped. Called by the frontend inside its View menu.
function ui.menu()
	local groups, order = {}, {}
	for _, panel in ipairs(panels) do
		if not groups[panel.group] then
			groups[panel.group] = {}
			order[#order + 1] = panel.group
		end
		local g = groups[panel.group]
		g[#g + 1] = panel
	end

	for _, name in ipairs(order) do
		if ig.BeginMenu(name, true) then
			for _, panel in ipairs(groups[name]) do
				local label = panel.failed and (panel.title .. "  (errored)") or panel.title
				if ig.MenuItem_Bool(label, nil, panel.open[0], not panel.failed) then
					panel.open[0] = not panel.open[0]
				end
			end
			ig.EndMenu()
		end
	end
end

local function report(panel, err)
	panel.failed = tostring(err)
	panel.open[0] = false
	print(string.format("[mse.ui] panel '%s' errored and was disabled: %s", panel.id, panel.failed))
end

-- Draws every visible panel. The host owns Begin/End so a script that throws
-- part way through cannot leave the window stack unbalanced.
function ui.draw()
	for _, panel in ipairs(panels) do
		if panel.open[0] and not panel.failed then
			local visible = ig.Begin(panel.title, panel.open, panel.flags)
			local ok, err = true, nil

			if visible then
				-- Snapshot inside our own window, so recovery unwinds whatever
				-- the panel left open -- a child, a table, a style push -- back
				-- to this point and no further. Snapshotting before Begin would
				-- have recovery close our window too, and the End below would
				-- then be unbalanced.
				ig.ErrorRecoveryStoreState(panel.recovery)
				ok, err = pcall(panel.draw)
				if not ok then
					ig.ErrorRecoveryTryToRecoverState(panel.recovery)
				end
			end

			ig.End()

			if not ok then
				report(panel, err)
			end
		end
	end
end

return ui
