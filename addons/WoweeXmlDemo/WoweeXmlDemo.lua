-- Referenced by the XML through <Script file="..."/>, which is how an addon
-- separates its handlers from its layout. This has to run before the frames are
-- built, because their scripts name these functions.

function WoweeXmlDemo_OnLoad(self)
    self.clicks = 0
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("WoweeXmlDemo: OnLoad ran on " .. tostring(self:GetName()))
    end
end

function WoweeXmlDemo_OnClick(self, button)
    self.clicks = (self.clicks or 0) + 1
    WoweeXmlDemoFrameCount:SetText("clicks: " .. self.clicks)
end
