#pragma once

// The marker that opens the first message of a chat's context when an older
// part of the chat has been replaced by a summary. ChatSessions writes it;
// BuddyProvider recognises it so it is never folded into the one-line note
// about earlier topics.
inline constexpr const char *kConversationSummaryPrefix = "[Conversation summary]";
