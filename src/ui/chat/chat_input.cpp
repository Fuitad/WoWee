#include "ui/chat/chat_input.hpp"

namespace wowee { namespace ui {

// Insert a spell / item link into the chat input buffer (shift-click).
void ChatInput::insertLink(const std::string& link) {
    if (link.empty()) return;
    size_t curLen = std::strlen(buffer_);
    if (curLen + link.size() + 1 < sizeof(buffer_)) {
        strncat(buffer_, link.c_str(), sizeof(buffer_) - curLen - 1);
        moveCursorToEnd_ = true;
        focusRequested_  = true;
    }
}

// Activate the input field with a leading '/' (slash key).
void ChatInput::activateSlashInput() {
    focusRequested_ = true;
    buffer_[0] = '/';
    buffer_[1] = '\0';
    moveCursorToEnd_ = true;
}

} // namespace ui
} // namespace wowee
