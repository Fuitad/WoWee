-- Proof that an addon can put something on the screen.
--
-- Every call below used to reach a table of no-op methods: CreateTexture handed
-- back an object whose SetTexture did nothing, so an addon could be written,
-- loaded and run without ever drawing a pixel. The same calls now build a real
-- widget tree the renderer walks.
--
-- Nothing here is special-cased. It is the ordinary addon API, and the art is
-- the player's own Interface files.

local f = CreateFrame("Frame", "WoweeWidgetDemoFrame", UIParent)
f:SetSize(256, 128)
f:SetPoint("CENTER", UIParent, "CENTER", 0, 120)
f:SetFrameStrata("HIGH")

-- A solid backdrop, drawn behind everything else in this frame.
local bg = f:CreateTexture(nil, "BACKGROUND")
bg:SetAllPoints(f)
bg:SetTexture(0, 0, 0, 0.55)

-- Real art from the game's own files, sized and anchored rather than centred by
-- luck: pinned to the left edge, vertically centred, inset by 8.
local icon = f:CreateTexture(nil, "ARTWORK")
icon:SetTexture("Interface\\Buttons\\Button-Backpack-Up")
icon:SetSize(64, 64)
icon:SetPoint("LEFT", f, "LEFT", 8, 0)

-- Drawn over the icon, to show layer ordering is honoured: OVERLAY beats
-- ARTWORK regardless of the order the regions were created in.
local shine = f:CreateTexture(nil, "OVERLAY")
shine:SetTexture("Interface\\Buttons\\UI-Panel-Button-Highlight")
shine:SetSize(64, 64)
shine:SetPoint("LEFT", f, "LEFT", 8, 0)
shine:SetVertexColor(1, 1, 1, 0.35)

local title = f:CreateFontString(nil, "OVERLAY")
title:SetPoint("TOPLEFT", f, "TOPLEFT", 80, -12)
title:SetSize(160, 20)
title:SetJustifyH("LEFT")
title:SetText("Widget system is live")

local detail = f:CreateFontString(nil, "OVERLAY")
detail:SetPoint("TOPLEFT", f, "TOPLEFT", 80, -40)
detail:SetSize(160, 40)
detail:SetJustifyH("LEFT")
detail:SetText("Real anchors, real Interface art")

-- Anchored to a sibling rather than the parent, which is how most of FrameXML
-- lays itself out.
local footer = f:CreateFontString(nil, "OVERLAY")
footer:SetPoint("TOPLEFT", detail, "BOTTOMLEFT", 0, -4)
footer:SetSize(160, 20)
footer:SetJustifyH("LEFT")
footer:SetText("/widgetdemo to toggle")

SLASH_WOWEEWIDGETDEMO1 = "/widgetdemo"
SlashCmdList["WOWEEWIDGETDEMO"] = function()
    if f:IsShown() then f:Hide() else f:Show() end
end
