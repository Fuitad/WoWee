#pragma once

/**
 * addon_lua_snippets.hpp — the Lua this client injects into the interface.
 *
 * Two of these are long enough to hide a mistake in: the options panels, which
 * build twelve categories of control out of the settings schema, and the coin
 * clearance, which reaches into MoneyFrame_Update. They lived as raw string
 * literals inside the functions that ran them, where a syntax error is not a
 * build failure — executeString simply answers false and the client carries on
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
-- and the anti-aliasing dropdown while FSR 3 was doing its own — controls that
-- answer, save, and change nothing.
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
local function newLayout(panel)
    return {panel = panel, column = 1, y = COLUMN_TOP}
end

local function reserve(layout, height)
    if layout.y - height < COLUMN_BOTTOM and layout.column == 1 then
        layout.column = 2
        layout.y = COLUMN_TOP
    end
    local x, y = COLUMN_X[layout.column], layout.y
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
    rule:SetWidth(COLUMN_WIDTH)
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
        -- Ticking one of these can be what makes another control live —
        -- normal mapping gates its strength, each extra bar gates its offsets
        -- — so the rest of the panel is re-read. A click is one event, unlike
        -- a slider drag, so there is nothing to throttle here.
        if onChanged then onChanged(setting.key) end
    end)
    withTooltip(button, setting.label, setting.tooltip)
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
    slider:SetWidth(COLUMN_WIDTH - 20)
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
    withTooltip(slider, setting.label, setting.tooltip)
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
    UIDropDownMenu_SetWidth(dropdown, COLUMN_WIDTH - 60)

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
                -- A dropdown can change other settings — the quality preset
                -- sets nine of them — so the rest of the panel is re-read.
                -- Only dropdowns do this: a slider would do it on every frame
                -- of a drag.
                if onChanged then onChanged(setting.key) end
            end
            UIDropDownMenu_AddButton(info, level)
        end
    end)
    withTooltip(dropdown, setting.label, setting.tooltip)
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

local function buildPanel(category, settings)
    local panel = CreateFrame("Frame", "WoweeOptions" .. slug(category))
    panel.name = category
    panel.parent = ROOT

    local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", 16, -16)
    title:SetText(ROOT .. " — " .. category)

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
    -- function that did nothing — the schema had no defaults to put back. It
    -- has now, so the button does what it says for this panel's settings and
    -- leaves every other panel's alone.
    panel.default = function()
        for _, setting in ipairs(settings) do
            WoweeSetSetting(setting.key, tostring(setting.default))
        end
        panel.refresh()
    end

    InterfaceOptions_AddCategory(panel, true)
end

-- The root. It holds no controls of its own: what it is for is to say what
-- this client's own settings are, and where the six that are not here live.
local root = CreateFrame("Frame", "WoweeOptionsRoot")
root.name = ROOT

local rootTitle = root:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
rootTitle:SetPoint("TOPLEFT", 16, -16)
rootTitle:SetText(ROOT)

local blurb = root:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
blurb:SetPoint("TOPLEFT", 16, -48)
blurb:SetWidth(560)
blurb:SetJustifyH("LEFT")
blurb:SetJustifyV("TOP")
blurb:SetText("This client's own settings, under the headings below. "
    .. "Everything takes effect as you change it; Cancel puts back what was "
    .. "there when you opened the panel.")

local elsewhere = root:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
elsewhere:SetPoint("TOPLEFT", 16, -110)
elsewhere:SetText("In the game's own panels")

local elsewhereRule = root:CreateTexture(nil, "ARTWORK")
elsewhereRule:SetTexture("Interface\\Buttons\\WHITE8X8")
elsewhereRule:SetVertexColor(0.5, 0.42, 0.22, 0.7)
elsewhereRule:SetWidth(560)
elsewhereRule:SetHeight(1)
elsewhereRule:SetPoint("TOPLEFT", 16, -128)

local elsewhereText = root:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
elsewhereText:SetPoint("TOPLEFT", 16, -138)
elsewhereText:SetWidth(560)
elsewhereText:SetJustifyH("LEFT")
elsewhereText:SetJustifyV("TOP")
elsewhereText:SetText(
    "Some settings are driven by the game's own controls rather than repeated "
    .. "here, so that the two cannot disagree:\n\n"
    .. "|cffffd100Video|r  —  resolution, view distance, ground clutter\n"
    .. "|cffffd100Sound|r  —  enable sound, master, music, ambience, "
    .. "sound effects\n"
    .. "|cffffd100Interface|r  —  mouse look speed, the minimap clock, "
    .. "friendly nameplates\n"
    .. "|cffffd100Interface, Social|r  —  chat timestamps\n"
    .. "|cffffd100Key Bindings|r  —  every key")

-- What this build is. The version comes from the client rather than being
-- written here, where it would go stale the first time a tag was cut.
local aboutTitle = root:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
aboutTitle:SetPoint("TOPLEFT", 16, -260)
aboutTitle:SetText("About")

local aboutRule = root:CreateTexture(nil, "ARTWORK")
aboutRule:SetTexture("Interface\\Buttons\\WHITE8X8")
aboutRule:SetVertexColor(0.5, 0.42, 0.22, 0.7)
aboutRule:SetWidth(560)
aboutRule:SetHeight(1)
aboutRule:SetPoint("TOPLEFT", 16, -278)

local aboutText = root:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
aboutText:SetPoint("TOPLEFT", 16, -288)
aboutText:SetWidth(560)
aboutText:SetJustifyH("LEFT")
aboutText:SetJustifyV("TOP")
aboutText:SetText("WoWee — a World of Warcraft client\n"
    .. (WoweeVersion and WoweeVersion() or "") .. "\n\n"
    .. "Kelsi Davis  —  |cff66b3ffgithub.com/Kelsidavis/WoWee|r")

root.okay = function() end
root.cancel = function() end
root.default = function() end
root.refresh = function() end
InterfaceOptions_AddCategory(root, true)

for _, category in ipairs(order) do
    buildPanel(category, byCategory[category])
end
)LUA";

/// Move the coin amounts off the coins, and take off the coin textures the
/// interface adds — the money bar this client draws already has them in its
/// own art, and the second set reads as letters after each number.
inline constexpr const char* kCoinAmountClearanceLua = R"LUA(
-- Colourblind mode off, explicitly.
--
-- It is the one thing in the whole interface that writes a letter beside a
-- coin: MoneyFrame_Update's colourblind branch does SetText(gold ..
-- GOLD_AMOUNT_SYMBOL) and clears the coin pictures, where the ordinary branch
-- writes the amount alone and leaves the coins. Reported as letters next to the
-- coins in the backpack, which is that branch running.
--
-- Nothing in this FrameXML ever assigns the global — every one of its dozen
-- readers compares it against "1" and there is no writer — so it is nil unless
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
            -- second set on top of the first — and small, sliced and overlapping
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
