#pragma once

/**
 * addon_lua_snippets.hpp - the Lua this client injects into the interface.
 *
 * Two of these are long enough to hide a mistake in: the options panels, which
 * build twelve categories of control out of the settings schema, and the coin
 * clearance, which reaches into MoneyFrame_Update. They lived as raw string
 * literals inside the functions that ran them, where a syntax error is not a
 * build failure - executeString simply answers false and the client carries on
 * with a warning in a log that is warning-only.
 *
 * Out here they can be handed to Lua by a test, which is the only way a
 * mistake in them is found before a player finds it.
 */

namespace wowee {
namespace addons {

/// The client's own settings, as panels in FrameXML's Interface Options.
///
/// Built from WoweeSettingList() rather than written out, so a setting added to
/// the schema appears without anyone editing Lua.
inline constexpr const char* kWoweeOptionsPanelLua = R"LUA(
local list = WoweeSettingList and WoweeSettingList()
if not list or #list == 0 then return end

local ROOT = "WoWee"

-- The panel container is 623 wide and a little under 500 tall, so two columns
-- of roughly 300 fit side by side with room for a slider's own labels. A
-- category that outgrows both columns is a category that wants splitting;
-- rather than clip it, the layout keeps going down the second column and the
-- overflow is visible, which is the version of this failure someone notices.
-- Which of the three options frames each category belongs on. The game menu
-- has a button for each, and a setting the player cannot reach from one of
-- them may as well not exist.
-- Every setting by key, so a greyed control can name the one it waits on by
-- its label rather than by its schema key.
local byKey = {}
for _, s in ipairs(list) do byKey[s.key] = s end

local kCategoryHost = {
    ["Graphics"]     = "video",
    ["Upscaling"]    = "video",
    ["Display"]      = "video",
    ["Sound"]        = "audio",
}

local COLUMN_X      = {16, 326}
local COLUMN_TOP    = -52
local COLUMN_BOTTOM = -436
local COLUMN_WIDTH  = 290

-- Frame names are looked up in _G, so a category's name has to survive being
-- part of one. "Combat & HUD" would not.
local function slug(text)
    return (tostring(text):gsub("[^%a%d]", ""))
end

-- A number as a person reads it: no trailing zeros, and no lone point.
local function num(v)
    if v == math.floor(v) then return tostring(math.floor(v)) end
    return (string.format("%.2f", v):gsub("0+$", ""):gsub("%.$", ""))
end

-- Whether a control is worth offering yet, from the schema's own test against
-- another setting: "" always, "key" whenever that one is on, "key=2" and
-- "key!=2" comparing its value.
--
-- The same rule the settings window applies, because it is the same field.
-- Without it these panels offered the FSR quality dropdown with upscaling off
-- and the anti-aliasing dropdown while FSR 3 was doing its own - controls that
-- answer, save, and change nothing.
-- What a greyed control is waiting for, in words.
--
-- The schema states the condition as "shadows" or "upscaling!=2", which says
-- nothing to a player looking at a slider that will not move. Reported as
-- "drop downs are greyed out" with no idea which of them were deliberate.
local function waitingOn(setting)
    local test = setting.enabledwhen
    if not test or test == "" then return nil end
    local key, want = test:match("^(.-)!=(.*)$")
    local negated = key ~= nil
    if not key then key, want = test:match("^(.-)=(.*)$") end
    if not key then key = test end
    local other = byKey[key]
    local name = other and other.label or key
    if want == nil then
        return "Available when " .. name .. " is on."
    end
    -- The value by its own label where the schema names one, so the line reads
    -- "when Upscaling is not FSR 2" rather than "is not 2".
    local choice = want
    if other and other.choices and other.choices ~= "" then
        local i = 0
        for piece in (other.choices .. "|"):gmatch("([^|]*)|") do
            if tostring(i) == want then choice = piece break end
            i = i + 1
        end
    end
    if negated then return "Available when " .. name .. " is not " .. choice .. "." end
    return "Available when " .. name .. " is " .. choice .. "."
end

local function isEnabled(setting)
    local test = setting.enabledwhen
    if not test or test == "" then return true end
    local key, want = test:match("^(.-)!=(.*)$")
    if key then return WoweeGetSetting(key) ~= want end
    key, want = test:match("^(.-)=(.*)$")
    if key then return WoweeGetSetting(key) == want end
    local value = WoweeGetSetting(test)
    return value ~= nil and value ~= "" and value ~= "0"
end

-- Greyed rather than hidden, so the panel keeps its shape and a player can see
-- both that the setting exists and what it is waiting on.
local function setEnabled(widget, label, enabled)
    if widget.Enable and widget.Disable then
        if enabled then widget:Enable() else widget:Disable() end
    end
    if not label then return end
    -- The colour a label goes back to is the one it was created with, read
    -- once: a checkbox's is white and a slider's is the gold heading colour,
    -- and picking either would be wrong for the other half of the panel.
    if not label.woweeColor then
        local r, g, b = 1, 1, 1
        if label.GetTextColor then r, g, b = label:GetTextColor() end
        label.woweeColor = {r or 1, g or 1, b or 1}
    end
    if enabled then
        label:SetTextColor(label.woweeColor[1], label.woweeColor[2], label.woweeColor[3])
    else
        label:SetTextColor(0.5, 0.5, 0.5)
    end
end

-- The tooltip with the reason under it, when there is one.
local function joinReason(tip, reason)
    if not reason then return tip end
    if not tip or tip == "" then return reason end
    return tip .. "\n" .. reason
end

-- The game's own hover text, one line per line of the schema's tooltip.
local function withTooltip(widget, title, tip)
    if not tip or tip == "" then return end
    widget:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(title, 1, 1, 1)
        for line in tostring(tip):gmatch("[^\n]+") do
            GameTooltip:AddLine(line, nil, nil, nil, true)
        end
        GameTooltip:Show()
    end)
    widget:SetScript("OnLeave", function() GameTooltip:Hide() end)
end

-- Where the next control goes, and when to start the second column.
--
-- Measured from the panel rather than taken from COLUMN_X, because the three
-- frames that host these panels do not have containers of one width. The
-- Interface frame's is the 623 those constants describe; the Video frame's is
-- around four hundred, because a category list the same size sits inside a
-- narrower frame. Laid out at 326 regardless, the second column started past
-- the right edge and every control in it hung off the side of the panel.
--
-- Two columns only when both fit with a gap. Otherwise one, down the middle of
-- whatever there is: a column running off the edge is worse than a long one.
local function newLayout(panel)
    -- Both dimensions from the panel, because neither constant was right.
    --
    -- COLUMN_X put the second column at 326 with a width of 290, which needs a
    -- container 623 wide; the real one is 413, on the Interface frame as much
    -- as the Video one, so everything in column two hung off the right edge.
    -- Falling back to a single column then moved the overflow to the bottom:
    -- eleven graphics controls need more height than one column has, and
    -- COLUMN_BOTTOM at -436 was already past a panel 428 tall, so nothing
    -- wrapped and the tail ran off the page.
    --
    -- So: as many columns as fit at a width still worth having, and a bottom
    -- that is the panel's own.
    local width = panel:GetWidth() or 0
    local height = panel:GetHeight() or 0
    if width <= 0 then width = 413 end
    if height <= 0 then height = 428 end

    local margin, gap, minWidth = 12, 14, 170
    local columns, columnWidth = {}, 0
    local twoWide = (width - margin * 2 - gap) / 2
    if twoWide >= minWidth then
        columnWidth = math.floor(twoWide)
        columns = {margin, margin + columnWidth + gap}
    else
        columnWidth = math.max(minWidth, width - margin * 2)
        columns = {margin}
    end

    return {panel = panel, column = 1, y = COLUMN_TOP,
            columns = columns, columnWidth = columnWidth,
            bottom = -(height - 10)}
end

local function reserve(layout, height)
    if layout.y - height < layout.bottom and layout.column < #layout.columns then
        layout.column = layout.column + 1
        layout.y = COLUMN_TOP
    end
    local x, y = layout.columns[layout.column], layout.y
    layout.y = layout.y - height
    return x, y
end

local function addHeading(layout, text)
    local x, y = reserve(layout, 32)
    local label = layout.panel:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    label:SetPoint("TOPLEFT", x, y - 4)
    label:SetText(text)
    local rule = layout.panel:CreateTexture(nil, "ARTWORK")
    rule:SetTexture("Interface\\Buttons\\WHITE8X8")
    rule:SetVertexColor(0.5, 0.42, 0.22, 0.7)
    rule:SetWidth(layout.columnWidth)
    rule:SetHeight(1)
    rule:SetPoint("TOPLEFT", x, y - 22)
end

-- The three controls. Each answers a read function and a write function, so
-- one refresh walks all of them without caring which kind it is holding.

local function addCheckButton(layout, panel, setting, onChanged)
    local x, y = reserve(layout, 27)
    local name = panel:GetName() .. setting.key
    local button = CreateFrame("CheckButton", name, panel,
                               "InterfaceOptionsCheckButtonTemplate")
    button:SetPoint("TOPLEFT", x, y)
    _G[name .. "Text"]:SetText(setting.label)
    button:SetScript("OnClick", function(self)
        WoweeSetSetting(setting.key, self:GetChecked() and "1" or "0")
        -- Ticking one of these can be what makes another control live -
        -- normal mapping gates its strength, each extra bar gates its offsets
        -- - so the rest of the panel is re-read. A click is one event, unlike
        -- a slider drag, so there is nothing to throttle here.
        if onChanged then onChanged(setting.key) end
    end)
    withTooltip(button, setting.label,
                joinReason(setting.tooltip, waitingOn(setting)))
    return {
        read = function()
            button:SetChecked(WoweeGetSetting(setting.key) == "1")
            setEnabled(button, _G[name .. "Text"], isEnabled(setting))
        end,
        write = function(value) WoweeSetSetting(setting.key, value) end,
    }
end

local function addSlider(layout, panel, setting)
    local x, y = reserve(layout, 50)
    local name = panel:GetName() .. setting.key
    local slider = CreateFrame("Slider", name, panel, "OptionsSliderTemplate")
    slider:SetPoint("TOPLEFT", x + 4, y - 14)
    slider:SetWidth(layout.columnWidth - 20)
    slider:SetMinMaxValues(setting.min, setting.max)
    slider:SetValueStep(setting.step)
    _G[name .. "Low"]:SetText(num(setting.min))
    _G[name .. "High"]:SetText(num(setting.max))

    -- The value belongs beside the name rather than under the thumb: the
    -- template has nowhere to put a moving label, and a slider whose number is
    -- only in a tooltip is a slider nobody can set to a particular value.
    local function showValue(value)
        _G[name .. "Text"]:SetText(setting.label .. ":  " .. num(value))
    end
    slider:SetScript("OnValueChanged", function(self, value)
        showValue(value)
        WoweeSetSetting(setting.key, tostring(value))
    end)
    withTooltip(slider, setting.label,
                joinReason(setting.tooltip, waitingOn(setting)))
    return {
        read = function()
            local value = tonumber(WoweeGetSetting(setting.key)) or setting.min
            slider:SetValue(value)
            showValue(value)
            setEnabled(slider, _G[name .. "Text"], isEnabled(setting))
        end,
        write = function(value) WoweeSetSetting(setting.key, value) end,
    }
end

local function addDropdown(layout, panel, setting, onChanged)
    local x, y = reserve(layout, 50)
    local name = panel:GetName() .. setting.key

    local label = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    label:SetPoint("TOPLEFT", x + 2, y - 2)
    label:SetText(setting.label)

    local choices = {}
    for choice in tostring(setting.choices or ""):gmatch("[^|]+") do
        table.insert(choices, choice)
    end

    -- The dropdown template carries about sixteen units of its own inset on the
    -- left, so it is anchored back by that much to line its box up with the
    -- checkboxes above it.
    local dropdown = CreateFrame("Frame", name, panel, "UIDropDownMenuTemplate")
    dropdown:SetPoint("TOPLEFT", x - 14, y - 16)
    UIDropDownMenu_SetWidth(dropdown, layout.columnWidth - 60)

    local function selected()
        return math.floor(tonumber(WoweeGetSetting(setting.key)) or 0) + 1
    end
    UIDropDownMenu_Initialize(dropdown, function(self, level)
        for index, choice in ipairs(choices) do
            local info = UIDropDownMenu_CreateInfo()
            info.text = choice
            info.value = index
            info.checked = (index == selected())
            info.func = function(button)
                WoweeSetSetting(setting.key, tostring(button.value - 1))
                UIDropDownMenu_SetText(dropdown, choices[button.value])
                CloseDropDownMenus()
                -- A dropdown can change other settings - the quality preset
                -- sets nine of them - so the rest of the panel is re-read.
                -- Only dropdowns do this: a slider would do it on every frame
                -- of a drag.
                if onChanged then onChanged(setting.key) end
            end
            UIDropDownMenu_AddButton(info, level)
        end
    end)
    withTooltip(dropdown, setting.label,
                joinReason(setting.tooltip, waitingOn(setting)))
    return {
        read = function()
            UIDropDownMenu_SetText(dropdown, choices[selected()] or "")
            local enabled = isEnabled(setting)
            setEnabled(dropdown, label, enabled)
            -- A dropdown is a frame rather than a button, so it has no Enable
            -- of its own; this is what the interface's own panels call.
            if enabled then
                if UIDropDownMenu_EnableDropDown then UIDropDownMenu_EnableDropDown(dropdown) end
            else
                if UIDropDownMenu_DisableDropDown then UIDropDownMenu_DisableDropDown(dropdown) end
            end
        end,
        write = function(value) WoweeSetSetting(setting.key, value) end,
    }
end

-- One panel per category, in the order the schema first mentions each.
local order, byCategory = {}, {}
for _, setting in ipairs(list) do
    if not byCategory[setting.category] then
        byCategory[setting.category] = {}
        table.insert(order, setting.category)
    end
    table.insert(byCategory[setting.category], setting)
end

-- Every panel registered, and every heading they nest under, so both can be
-- opened once the whole list exists.
local registered = {}
local headings = {}

-- The frame a category's panel belongs to, and the container inside it.
--
-- Needed before the controls are laid out, not just before registration: the
-- layout measures the container to decide how many columns fit, and an
-- unparented panel has no width to measure.
local function hostContainerFor(category)
    local host = kCategoryHost[category]
    local frame
    if host == "video" then frame = VideoOptionsFrame
    elseif host == "audio" then frame = AudioOptionsFrame
    else frame = InterfaceOptionsFrame end
    -- .panelContainer, or the global the XML names it by. VideoOptionsFrame
    -- and AudioOptionsFrame set the field; InterfaceOptionsFrame does not, and
    -- only has InterfaceOptionsFramePanelContainer - so its panels were left
    -- unparented, sized zero, and laid out against nothing.
    local container = frame and frame.panelContainer
    if not container and frame and frame.GetName then
        container = _G[(frame:GetName() or "") .. "PanelContainer"]
    end
    return frame, container
end

local function buildPanel(category, settings)
    local panel = CreateFrame("Frame", "WoweeOptions" .. slug(category))
    -- Parented and sized before anything is laid out inside it. See
    -- hostContainerFor and newLayout.
    local _, container = hostContainerFor(category)
    if container then
        panel:SetParent(container)
        panel:ClearAllPoints()
        panel:SetAllPoints(container)
        panel:Hide()
    end
    panel.name = category
    panel.parent = ROOT

    local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", 16, -16)
    title:SetText(ROOT .. ": " .. category)

    local layout = newLayout(panel)
    local controls = {}
    local heading = nil
    -- Declared before the controls because a dropdown's handler calls it, and
    -- the controls are what it walks.
    local function rereadOthers(changedKey)
        for _, control in ipairs(controls) do
            if control.key ~= changedKey then control.read() end
        end
    end
    for _, setting in ipairs(settings) do
        if setting.section ~= "" and setting.section ~= heading then
            heading = setting.section
            addHeading(layout, heading)
        end
        local control
        if setting.kind == "bool" then
            control = addCheckButton(layout, panel, setting, rereadOthers)
        elseif setting.kind == "enum" then
            control = addDropdown(layout, panel, setting, rereadOthers)
        else
            control = addSlider(layout, panel, setting)
        end
        control.key = setting.key
        table.insert(controls, control)
    end

    -- Everything applies as it is changed, so Okay has nothing left to do.
    -- Cancel does: it puts back what was there when the panel was last shown,
    -- which is what the button promises and what the old version of this panel
    -- quietly did not honour.
    local opened = {}
    panel.refresh = function()
        for _, control in ipairs(controls) do
            opened[control.key] = WoweeGetSetting(control.key)
            control.read()
        end
    end
    panel.okay = function()
        for _, control in ipairs(controls) do
            opened[control.key] = WoweeGetSetting(control.key)
        end
    end
    panel.cancel = function()
        for _, control in ipairs(controls) do
            if opened[control.key] then control.write(opened[control.key]) end
        end
        panel.refresh()
    end
    -- The game puts a Defaults button on every options panel, and this was a
    -- function that did nothing - the schema had no defaults to put back. It
    -- has now, so the button does what it says for this panel's settings and
    -- leaves every other panel's alone.
    panel.default = function()
        for _, setting in ipairs(settings) do
            WoweeSetSetting(setting.key, tostring(setting.default))
        end
        panel.refresh()
    end

    -- Where a player will actually look for it.
    --
    -- These were all registered into Interface Options' AddOns tab, which is
    -- two levels down from the game menu and is where an addon's settings go -
    -- not the client's own. Reported as the options still being missing, and
    -- fairly: pressing Video showed the game's video panel and nothing of ours.
    --
    -- So each category goes to the frame its own button opens. The graphics
    -- ones join Video, the sound ones join Sound, and the rest join the
    -- Interface list beside the game's own categories.
    -- The panel has to be a child of the frame's panel container before it is
    -- registered. OptionsList_DisplayPanel positions it with
    --
    --     local panelContainer = panel:GetParent()
    --     panel:SetPoint("TOPLEFT", panelContainer, "TOPLEFT")
    --
    -- so the parent is what decides where it lands, and AddCategory does not
    -- set one - Blizzard's own panels are declared in XML as children of
    -- $parentPanelContainer and arrive parented. Ours were created with no
    -- parent at all, so every control drew from the screen's top-left corner,
    -- over the player frame and the chat log, while the panel it belonged to
    -- stayed empty.
    -- The panel was parented and sized in buildPanel, which the layout needs.
    local host = kCategoryHost[category]
    if host == "video" and VideoOptionsFrame and OptionsFrame_AddCategory then
        OptionsFrame_AddCategory(VideoOptionsFrame, panel)
    elseif host == "audio" and AudioOptionsFrame and OptionsFrame_AddCategory then
        OptionsFrame_AddCategory(AudioOptionsFrame, panel)
    else
        InterfaceOptions_AddCategory(panel)
    end
    table.insert(registered, panel)
end

-- A heading of our own on each frame that hosts one of our categories.
--
-- Both AddCategory functions nest a panel under an existing one whose name
-- matches panel.parent, so a heading registered first collects everything
-- after it. Without one our Sound category sat directly beside the game's own
-- Sound and the list read as two of the same thing.
local function addHostHeading(hostFrame, blurbText)
    local heading = CreateFrame("Frame", "WoweeOptionsHeading" .. tostring(hostFrame))
    -- Parented and sized like any other panel. Left unparented it anchored to
    -- the screen, so this heading's title and blurb were drawn over the player
    -- frame in the top-left corner rather than inside the options frame - the
    -- same fault the category panels had, in the one place that did not get
    -- the fix.
    local container = hostFrame and hostFrame.panelContainer
    if container then
        heading:SetParent(container)
        heading:ClearAllPoints()
        heading:SetAllPoints(container)
        heading:Hide()
    end
    heading.name = ROOT
    local title = heading:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", 16, -16)
    title:SetText(ROOT)
    local blurb = heading:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
    blurb:SetPoint("TOPLEFT", 16, -48)
    -- Inside the panel rather than 560 wide, which is wider than the 413 the
    -- container actually is.
    blurb:SetWidth(math.max(200, (container and container:GetWidth() or 413) - 32))
    blurb:SetJustifyH("LEFT")
    blurb:SetJustifyV("TOP")
    blurb:SetText(blurbText)
    heading.okay = function() end
    heading.cancel = function() end
    heading.default = function() end
    heading.refresh = function() end
    if hostFrame and OptionsFrame_AddCategory then
        OptionsFrame_AddCategory(hostFrame, heading)
    end
    table.insert(headings, heading)
    return heading
end

-- The root. It holds no controls of its own: what it is for is to say what
-- this client's own settings are, and where the six that are not here live.
-- The root panel is laid out by hand rather than generated, so the anchors
-- below carry the room each block needs as a "needs N" note. A test reads
-- those and checks nothing is placed inside anything else - which is how a
-- search box came to be drawn straight through the two blocks under it, with
-- every behavioural check still passing.
local root = CreateFrame("Frame", "WoweeOptionsRoot")
root.name = ROOT

local rootTitle = root:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
rootTitle:SetPoint("TOPLEFT", 16, -16) -- needs 22
rootTitle:SetText(ROOT)

local blurb = root:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
blurb:SetPoint("TOPLEFT", 16, -48) -- needs 42
blurb:SetWidth(560)
blurb:SetJustifyH("LEFT")
blurb:SetJustifyV("TOP")
blurb:SetText("This client's own settings, under the headings below. "
    .. "Everything takes effect as you change it; Cancel puts back what was "
    .. "there when you opened the panel.")

local elsewhere = root:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
elsewhere:SetPoint("TOPLEFT", 16, -240) -- needs 14
elsewhere:SetText("In the game's own panels")

local elsewhereRule = root:CreateTexture(nil, "ARTWORK")
elsewhereRule:SetTexture("Interface\\Buttons\\WHITE8X8")
elsewhereRule:SetVertexColor(0.5, 0.42, 0.22, 0.7)
elsewhereRule:SetWidth(560)
elsewhereRule:SetHeight(1)
elsewhereRule:SetPoint("TOPLEFT", 16, -258) -- needs 2

local elsewhereText = root:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
elsewhereText:SetPoint("TOPLEFT", 16, -268) -- needs 84
elsewhereText:SetWidth(560)
elsewhereText:SetJustifyH("LEFT")
elsewhereText:SetJustifyV("TOP")
elsewhereText:SetText(
    "Some settings are driven by the game's own controls rather than repeated "
    .. "here, so that the two cannot disagree:\n\n"
    .. "|cffffd100Video|r  ..  resolution, view distance, ground clutter\n"
    .. "|cffffd100Sound|r  ..  enable sound, master, music, ambience, "
    .. "sound effects\n"
    .. "|cffffd100Interface|r  ..  mouse look speed, the minimap clock, "
    .. "friendly nameplates\n"
    .. "|cffffd100Interface, Social|r  ..  chat timestamps\n"
    .. "|cffffd100Key Bindings|r  ..  every key")

-- What this build is. The version comes from the client rather than being
-- written here, where it would go stale the first time a tag was cut.
local aboutTitle = root:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
aboutTitle:SetPoint("TOPLEFT", 16, -370) -- needs 14
aboutTitle:SetText("About")

local aboutRule = root:CreateTexture(nil, "ARTWORK")
aboutRule:SetTexture("Interface\\Buttons\\WHITE8X8")
aboutRule:SetVertexColor(0.5, 0.42, 0.22, 0.7)
aboutRule:SetWidth(560)
aboutRule:SetHeight(1)
aboutRule:SetPoint("TOPLEFT", 16, -388) -- needs 2

local aboutText = root:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
aboutText:SetPoint("TOPLEFT", 16, -398) -- needs 42
aboutText:SetWidth(560)
aboutText:SetJustifyH("LEFT")
aboutText:SetJustifyV("TOP")
aboutText:SetText("WoWee, a World of Warcraft client\n"
    .. (WoweeVersion and WoweeVersion() or "") .. "\n\n"
    .. "Kelsi Davis  ..  |cff66b3ffgithub.com/Kelsidavis/WoWee|r")

-- Find a setting without knowing which panel it is on.
--
-- Seventy-odd settings across twelve panels is enough that a player looking
-- for one has to guess, and guessing wrong twice is how a setting comes to be
-- reported missing. Typing here lists what matches and, more to the point,
-- says which panel each one is on.
local searchTitle = root:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
searchTitle:SetPoint("TOPLEFT", 16, -104) -- needs 14
searchTitle:SetText("Find a setting")

local searchRule = root:CreateTexture(nil, "ARTWORK")
searchRule:SetTexture("Interface\\Buttons\\WHITE8X8")
searchRule:SetVertexColor(0.5, 0.42, 0.22, 0.7)
searchRule:SetWidth(560)
searchRule:SetHeight(1)
searchRule:SetPoint("TOPLEFT", 16, -122) -- needs 2

local searchBox = CreateFrame("EditBox", "WoweeOptionsSearchBox", root, "InputBoxTemplate")
searchBox:SetPoint("TOPLEFT", 22, -130) -- needs 22
searchBox:SetWidth(280)
searchBox:SetHeight(20)
searchBox:SetAutoFocus(false)

-- Named, so what the search decided can be read back from outside - the
-- headless runner cannot enumerate a frame's regions.
local searchResults = root:CreateFontString("WoweeOptionsSearchResults",
                                           "ARTWORK", "GameFontHighlightSmall")
searchResults:SetPoint("TOPLEFT", 16, -158) -- needs 78
searchResults:SetWidth(560)
searchResults:SetJustifyH("LEFT")
searchResults:SetJustifyV("TOP")

local function lower(text)
    return tostring(text):lower()
end

local function runSearch(query)
    query = lower(query)
    if query == "" then
        searchResults:SetText("")
        return
    end
    local found, shown = 0, {}
    for _, setting in ipairs(list) do
        if lower(setting.label):find(query, 1, true) or
           lower(setting.key):find(query, 1, true) then
            found = found + 1
            -- Five is what fits between the box and the next heading; the
            -- count below says how many more rather than pretending these are
            -- all of them. Eight was the first guess and it ran the results
            -- straight through the two blocks under it.
            if found <= 5 then
                shown[#shown + 1] = "|cffffd100" .. setting.label ..
                                    "|r  in  " .. setting.category
            end
        end
    end
    if found == 0 then
        searchResults:SetText("|cff909090No setting matches that.|r")
    elseif found > 5 then
        searchResults:SetText(table.concat(shown, "\n") ..
            "\n|cff909090... and " .. (found - 5) .. " more|r")
    else
        searchResults:SetText(table.concat(shown, "\n"))
    end
end

searchBox:SetScript("OnTextChanged", function(self) runSearch(self:GetText()) end)
searchBox:SetScript("OnEscapePressed", function(self) self:SetText("") self:ClearFocus() end)

root.okay = function() end
root.cancel = function() end
root.default = function() end
root.refresh = function() end
-- The Game tab, not the AddOns one. This is the client's own settings, not an
-- addon's, and the AddOns tab is two levels down from the button a player
-- presses.
InterfaceOptions_AddCategory(root)

-- The headings first: a category can only nest under a name already in that
-- frame's list.
if VideoOptionsFrame then
    addHostHeading(VideoOptionsFrame,
        "This client's own graphics settings, under the headings below. "
        .. "The game's own Resolution and Effects panels are above.")
end
if AudioOptionsFrame then
    addHostHeading(AudioOptionsFrame,
        "This client's own sound settings. The game's Sound panel above "
        .. "carries the master, music, ambience and effects volumes.")
end

-- Backwards, because a nested category is inserted directly after its parent:
-- registering in schema order would list them in the opposite one.
for i = #order, 1, -1 do
    buildPanel(order[i], byCategory[order[i]])
end

-- Open, rather than folded away behind a plus sign.
--
-- Nesting a category hides it: AddCategory marks the new parent collapsed and
-- every child hidden, which is right for the game's own sub-panels and wrong
-- here. Everything this client adds is nested, so a player who opened Video
-- looking for it would find one row reading WoWee and nothing else - the same
-- thing as missing, which is how it was reported.
--
-- This is the state the toggle leaves them in, written directly because there
-- is no button to click yet: the parents are open and the children are not
-- hidden. Clicking the toggle still folds them away afterwards.
table.insert(headings, root)
for _, heading in ipairs(headings) do
    heading.collapsed = false
end
for _, panel in ipairs(registered) do
    panel.hidden = false
end
if VideoOptionsFrame and VideoOptionsFrame.categoryFrame then
    VideoOptionsFrame.categoryFrame:update()
end
if AudioOptionsFrame and AudioOptionsFrame.categoryFrame then
    AudioOptionsFrame.categoryFrame:update()
end
if InterfaceCategoryList_Update then InterfaceCategoryList_Update() end
)LUA";

/// Grey out the two Sound sliders that are not settings on this client.
///
/// Sound Quality and Sound Channels are real controls in the game's Sound
/// panel and mean nothing here: miniaudio mixes every voice it is given at the
/// device's own rate, so there is no channel cap to raise and no quality tier
/// to pick. Both answer their maximum now (see pushCvarDefault), but a slider
/// sitting at the top of its range still invites a player to drag it down and
/// then wonder why nothing changed.
///
/// Disabled and greyed, which is the same thing the Refresh dropdown two
/// panels away does for the same reason - the setting is visible, and visibly
/// not a choice.
inline constexpr const char* kAudioFixedSlidersLua = R"LUA(
-- By name, not by frame: a nil frame used as a table key raises outright,
-- which would take the whole snippet with it.
local kFixed = {
    {"AudioOptionsSoundPanelSoundQuality",
     "This client mixes at the device's own rate. There is no lower quality to select."},
    {"AudioOptionsSoundPanelSoundChannels",
     "This client does not cap the number of voices it mixes."},
}
for _, entry in ipairs(kFixed) do
    local slider, why = _G[entry[1]], entry[2]
    if slider and slider.GetName then
        if slider.Disable then slider:Disable() end
        local label = _G[slider:GetName() .. "Text"]
        if label and label.SetVertexColor then
            label:SetVertexColor(GRAY_FONT_COLOR.r, GRAY_FONT_COLOR.g, GRAY_FONT_COLOR.b)
        end
        slider:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:SetText(label and label:GetText() or "", 1, 1, 1)
            GameTooltip:AddLine(why, nil, nil, nil, true)
            GameTooltip:Show()
        end)
        slider:SetScript("OnLeave", function() GameTooltip:Hide() end)
    end
end
)LUA";

/// Move the coin amounts off the coins, and take off the coin textures the
/// interface adds - the money bar this client draws already has them in its
/// own art, and the second set reads as letters after each number.
/// Populating a dropdown is not opening one, so it does not get the sound.
///
/// UIDropDownMenu_Initialize calls its initialize function straight away -
/// stock behaviour, not ours - and every unit frame's initializer ends in
/// UnitPopup_ShowMenu, which finishes with PlaySound("igMainMenuOpen"). The
/// player frame, four party frames and three target frames all initialize when
/// the player enters the world, so eight copies of uEscapeScreenOpen.wav land
/// inside thirty milliseconds and stack into one loud hit.
///
/// The real client makes the same calls and is silent: they happen behind a
/// loading screen with the sound system not yet up. Ours has audio running by
/// then, so the difference is audible and reads as a jump scare.
///
/// This is the half that catches the initializers driven by the world-entry
/// packet, which arrives long after the interface has loaded. The load itself
/// is covered from C by LuaEngine::setUiSoundsSuppressed, because no script of
/// ours can run early enough for that.
///
/// Hooked rather than edited into unitpopup.lua, so the interface's own files
/// stay Blizzard's. Restored through pcall, so an initializer that raises
/// cannot leave every interface sound muted for the session.
/// Ask before keeping a new interface scale, and put the old one back if
/// nobody answers.
///
/// The scale applies as the slider moves - that is the shipped behaviour, and
/// it is what makes the control usable at all. It also means a scale you
/// cannot read is applied before you can decide whether you want it, and the
/// way out of that is the options frame you have just made unreadable.
/// WidgetTree::kMaxUserScale keeps it from ever reaching that, and this is the
/// second line of defence: fifteen seconds to say keep, or it goes back.
///
/// StaticPopup already implements exactly this - StaticPopup_OnUpdate calls
/// OnCancel with the reason "timeout" when timeleft runs out - so the dialog
/// is a registration rather than a mechanism. The countdown in the text is
/// ours, because the live update in StaticPopup_OnUpdate only runs for a
/// hardcoded list of dialog names; the dialog's own OnUpdate is called for
/// everything, so the number is written from there.
///
/// Hooked, not edited into videooptionspanels.lua: the interface data is
/// extracted game content and not somewhere our changes can live.
inline constexpr const char* kUiScaleConfirmLua = R"LUA(
local kRevertSeconds = 15

StaticPopupDialogs["WOWEE_CONFIRM_UI_SCALE"] = {
    text = "Keep this interface scale?",
    button1 = KEEP_THIS_CHANGE or "Keep",
    button2 = CANCEL or "Cancel",
    timeout = kRevertSeconds,
    whileDead = 1,
    -- Escape would dismiss the dialog and leave the untried scale applied,
    -- which is the state this exists to prevent.
    hideOnEscape = 0,
    OnAccept = function(self, data)
        if data then data.baseline = nil end
    end,
    OnCancel = function(self, data, reason)
        if data and data.baseline then
            SetCVar("uiscale", data.baseline)
            local slider = VideoOptionsResolutionPanelUIScaleSlider
            if slider and slider.SetDisplayValue then
                slider:SetDisplayValue(tonumber(data.baseline) or 1)
            end
        end
    end,
    OnUpdate = function(self, elapsed)
        local text = _G[self:GetName() .. "Text"]
        if text and self.timeleft then
            text:SetFormattedText("Keep this interface scale?\n\nReverting in %d seconds.",
                                  math.ceil(self.timeleft))
        end
    end,
}

-- The baseline is read when the panel is shown rather than when the slider
-- moves: a drag is many changes and only the first of them knows what the
-- scale was before any of this started.
local panel = VideoOptionsResolutionPanel
if panel and panel.HookScript then
    panel:HookScript("OnShow", function(self)
        self.woweeUiScaleBaseline = GetCVar("uiscale")
    end)
end

local okay = VideoOptionsFrameOkay
if okay and okay.HookScript then
    okay:HookScript("OnClick", function()
        local p = VideoOptionsResolutionPanel
        local baseline = p and p.woweeUiScaleBaseline
        local current = GetCVar("uiscale")
        if baseline and current and baseline ~= current then
            StaticPopup_Show("WOWEE_CONFIRM_UI_SCALE", nil, nil,
                             { baseline = baseline })
            if p then p.woweeUiScaleBaseline = current end
        end
    end)
end
)LUA";

inline constexpr const char* kDropdownInitSilenceLua = R"LUA(
local realPlaySound = PlaySound
local silent = false
PlaySound = function(...)
    if silent then return end
    return realPlaySound(...)
end

local realInitialize = UIDropDownMenu_Initialize
if realInitialize then
    UIDropDownMenu_Initialize = function(...)
        local was = silent
        silent = true
        local ok, err = pcall(realInitialize, ...)
        silent = was
        if not ok then error(err, 0) end
    end
end
)LUA";

inline constexpr const char* kCoinAmountClearanceLua = R"LUA(
-- Colourblind mode off, explicitly.
--
-- It is the one thing in the whole interface that writes a letter beside a
-- coin: MoneyFrame_Update's colourblind branch does SetText(gold ..
-- GOLD_AMOUNT_SYMBOL) and clears the coin pictures, where the ordinary branch
-- writes the amount alone and leaves the coins. Reported as letters next to the
-- coins in the backpack, which is that branch running.
--
-- Nothing in this FrameXML ever assigns the global - every one of its dozen
-- readers compares it against "1" and there is no writer - so it is nil unless
-- something outside sets it, and nil is not "0" either. Saying so plainly is
-- cheaper than finding out what set it.
ENABLE_COLORBLIND_MODE = "0"

-- Between an amount and its own coin. Re-anchoring the buttons to each other
-- as well was tried and put copper two units worse than it started: their
-- spacing is MoneyFrame_Update's own, and it is right.
local kClearance = 6

local function nudge(frameName)
    for _, coin in ipairs({"Gold", "Silver", "Copper"}) do
        local text = _G[frameName .. coin .. "ButtonText"]
        local button = _G[frameName .. coin .. "Button"]
        if text and button then
            -- The amount alone, whatever wrote it.
            --
            -- WoW writes the amount and the coin's picture; the letter belongs
            -- to the colourblind branch, which is off. It has been reported
            -- four times running and turning that branch off did not stop it,
            -- so rather than keep hunting for the writer, the letter comes off
            -- here where the answer is certain. If the diagnostic in the
            -- renderer ever names what puts it there, this can go.
            local shown = text:GetText()
            if shown then
                local bare = shown:match("^(%d+)[gsc]$")
                if bare then text:SetText(bare) end
            end
            -- No coin of ours.
            --
            -- MoneyFrame_Update makes a texture per denomination and slices the
            -- coin out of UI-MoneyIcons for it. The money bar this client draws
            -- already carries the coins in its own art, so those three are a
            -- second set on top of the first - and small, sliced and overlapping
            -- the amounts, they read as letters after each number. Four reports
            -- of "letters next to the coins" are that.
            --
            -- The amount then wants the whole button, since nothing sits to its
            -- right any more.
            local icon = button:GetNormalTexture()
            if icon then icon:SetTexture(nil) end
            text:ClearAllPoints()
            text:SetPoint("RIGHT", button, "RIGHT", -kClearance, 0)
        end
    end
end

local original = MoneyFrame_Update
if type(original) == "function" then
    MoneyFrame_Update = function(frameName, money, ...)
        original(frameName, money, ...)
        -- The frame may be named or handed over as a table, as the original
        -- accepts both.
        local name = frameName
        if type(name) == "table" then name = name:GetName() end
        if name then nudge(name) end
    end
end
)LUA";

}  // namespace addons
}  // namespace wowee
