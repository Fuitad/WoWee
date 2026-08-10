-- Every bag in one window, with a search box and a sort button.
--
-- 3.3.5's interface has no such thing: it opens one ContainerFrame per bag and
-- has no sorting at all, both of which arrived years later. This client's own
-- bag window has had a combined view and a Sort Bags button for a long time,
-- but that is drawn by the client — hand the bags to FrameXML and it goes away
-- with them. This is the same idea built out of the ordinary addon API, so it
-- is there whichever side is drawing.
--
-- Nothing here is special-cased. GetContainerNumSlots, GetContainerItemInfo,
-- PickupContainerItem and GameTooltip:SetBagItem are the interface's own, and
-- SortBags is this client's, which runs the sort the bag window already used.

local COLS       = 10      -- slots per row
local SLOT       = 37      -- button size
local PAD        = 4       -- gap between slots
local EDGE       = 14      -- frame border inset
local TOP        = 62      -- room for the title, search box and buttons
local BOTTOM     = 34      -- room for the money line
local BAGS       = {0, 1, 2, 3, 4}   -- backpack, then the four worn bags

local buttons = {}         -- flat list, reused across redraws
local search  = ""

-- ── The frame ───────────────────────────────────────────────────────────────

local f = CreateFrame("Frame", "WoweeAllBagsFrame", UIParent)
f:SetFrameStrata("HIGH")
f:SetToplevel(true)
f:SetMovable(true)
f:EnableMouse(true)
f:RegisterForDrag("LeftButton")
f:SetScript("OnDragStart", function(self) self:StartMoving() end)
f:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
f:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
f:SetBackdrop({
    bgFile   = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
f:Hide()

local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", f, "TOP", 0, -16)
title:SetText("All Bags")

local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -8, -8)

-- Sort. Disabled while the moves are still going out, because a sort is dozens
-- of swaps and the client sends them a tick at a time.
local sort = CreateFrame("Button", "WoweeAllBagsSort", f, "UIPanelButtonTemplate")
sort:SetSize(64, 21)
sort:SetPoint("TOPLEFT", f, "TOPLEFT", EDGE, -34)
sort:SetText("Sort")
sort:SetScript("OnClick", function()
    if SortBags then SortBags() end
end)

-- Search. Dims what does not match rather than hiding it, so the slots keep
-- their places and nothing jumps around under the cursor.
local box = CreateFrame("EditBox", "WoweeAllBagsSearch", f, "InputBoxTemplate")
box:SetSize(150, 20)
box:SetPoint("LEFT", sort, "RIGHT", 12, 0)
box:SetAutoFocus(false)
box:SetScript("OnTextChanged", function(self)
    search = string.lower(self:GetText() or "")
    WoweeAllBags_Update()
end)
box:SetScript("OnEscapePressed", function(self) self:SetText("") self:ClearFocus() end)

local counts = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
counts:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", EDGE + 2, 14)

-- ── Slots ───────────────────────────────────────────────────────────────────

local function slotButton(index)
    local b = buttons[index]
    if b then return b end
    b = CreateFrame("Button", "WoweeAllBagsItem" .. index, f)
    b:SetSize(SLOT, SLOT)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")

    b.icon = b:CreateTexture(nil, "BACKGROUND")
    b.icon:SetAllPoints(b)

    b.count = b:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
    b.count:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -2, 2)

    b:SetScript("OnEnter", function(self)
        if not self.bag then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetBagItem(self.bag, self.slot)
        GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    b:SetScript("OnClick", function(self, button)
        if not self.bag then return end
        if button == "RightButton" then UseContainerItem(self.bag, self.slot)
        else                            PickupContainerItem(self.bag, self.slot) end
        WoweeAllBags_Update()
    end)

    buttons[index] = b
    return b
end

-- ── Redraw ──────────────────────────────────────────────────────────────────

function WoweeAllBags_Update()
    if not f:IsShown() then return end

    local shown, used = 0, 0
    for _, bag in ipairs(BAGS) do
        local size = GetContainerNumSlots(bag) or 0
        for slot = 1, size do
            shown = shown + 1
            local b = slotButton(shown)
            b.bag, b.slot = bag, slot

            local col, row = (shown - 1) % COLS, math.floor((shown - 1) / COLS)
            b:ClearAllPoints()
            b:SetPoint("TOPLEFT", f, "TOPLEFT",
                       EDGE + col * (SLOT + PAD), -(TOP + row * (SLOT + PAD)))

            local texture, count = GetContainerItemInfo(bag, slot)
            if texture then
                used = used + 1
                b.icon:SetTexture(texture)
                b.count:SetText((count and count > 1) and count or "")
                -- Dimmed rather than hidden while a search is running, so a
                -- slot never moves out from under the cursor mid-look.
                local match = true
                if search ~= "" then
                    local link = GetContainerItemLink(bag, slot)
                    local name = link and string.match(link, "%[(.-)%]") or ""
                    match = string.find(string.lower(name), search, 1, true) ~= nil
                end
                b.icon:SetVertexColor(1, 1, 1, match and 1 or 0.25)
            else
                b.icon:SetTexture("Interface\\Buttons\\UI-EmptySlot-White")
                b.icon:SetVertexColor(1, 1, 1, 0.35)
                b.count:SetText("")
            end
            b:Show()
        end
    end

    -- Anything left from a larger set of bags stays built but out of the way.
    for i = shown + 1, #buttons do buttons[i]:Hide() end

    local rows = math.max(1, math.ceil(shown / COLS))
    f:SetWidth(EDGE * 2 + COLS * (SLOT + PAD) - PAD)
    f:SetHeight(TOP + rows * (SLOT + PAD) - PAD + BOTTOM)

    counts:SetText(used .. "/" .. shown .. " slots used")
    if sort.SetEnabled then
        sort:SetEnabled(not (IsSortingBags and IsSortingBags()))
    end
end

-- ── Events and the way in ───────────────────────────────────────────────────

f:SetScript("OnEvent", function() WoweeAllBags_Update() end)
f:RegisterEvent("BAG_UPDATE")
f:RegisterEvent("ITEM_LOCK_CHANGED")
f:RegisterEvent("PLAYER_MONEY")

function WoweeAllBags_Toggle()
    if f:IsShown() then
        f:Hide()
    else
        f:Show()
        WoweeAllBags_Update()
    end
end

SLASH_WOWEEALLBAGS1 = "/allbags"
SLASH_WOWEEALLBAGS2 = "/bags"
SlashCmdList["WOWEEALLBAGS"] = WoweeAllBags_Toggle
