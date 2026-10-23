#include "BuddyProviderInternal.h"

using namespace noch::buddyprovider;

// Context editing (Anthropic "clear tool uses" strategy), in batches.
//
// The newest kKeepRecentToolResults tool results always stay verbatim. Older
// bulky results (and every older screenshot) are replaced by a short
// placeholder - but not one at a time. Rewriting a message early in the
// conversation changes the prompt prefix, which throws away the prompt cache
// for everything after it, so trimming every turn meant paying for the whole
// history again on every turn. Instead nothing is touched until enough has
// piled up (kClearBatch*), then all of it goes at once and the prefix stays
// byte-identical for the next several turns. This is what `clear_at_least`
// is for in Anthropic's context-editing API. Returns how many results were
// cleared this time.
int BuddyProvider::compactContext()
{
    struct Ref { int msg; int block; };   // block -1 = whole OpenAI-style tool message
    QList<Ref> refs;
    for (int i = 0; i < m_agentMessages.size(); ++i) {
        const QJsonObject m = m_agentMessages.at(i).toObject();
        const QString role = m.value(QStringLiteral("role")).toString();
        if (role == QLatin1String("tool")) {
            refs.append({ i, -1 });
        } else if (role == QLatin1String("user") && m.value(QStringLiteral("content")).isArray()) {
            const QJsonArray c = m.value(QStringLiteral("content")).toArray();
            for (int b = 0; b < c.size(); ++b)
                if (c.at(b).toObject().value(QStringLiteral("type")).toString() == QLatin1String("tool_result"))
                    refs.append({ i, b });
        }
    }
    if (refs.size() <= kKeepRecentToolResults)
        return 0;

    // What each older result is: nothing to clear, bulky text, or a screenshot.
    struct Candidate { Ref ref; int chars; bool image; };   // chars: for the placeholder text
    QList<Candidate> candidates;
    int images = 0;
    for (int r = 0; r < refs.size() - kKeepRecentToolResults; ++r) {
        const Ref ref = refs.at(r);
        const QJsonObject m = m_agentMessages.at(ref.msg).toObject();
        int chars = 0;
        bool hasImage = false;
        bool already = false;
        if (ref.block < 0) {
            // OpenAI-style tool message. Since vision support, its content can
            // be an array: [{type:"text",...},{type:"image_url",...}], and
            // Ollama carries screenshots in a top-level "images" array.
            const QJsonValue content = m.value(QStringLiteral("content"));
            const bool ollamaImages = !m.value(QStringLiteral("images")).toArray().isEmpty();
            if (content.isArray()) {
                bool stillHasImage = ollamaImages;
                for (const QJsonValue &part : content.toArray()) {
                    const QJsonObject po = part.toObject();
                    if (po.value(QStringLiteral("type")).toString() == QLatin1String("image_url"))
                        stillHasImage = true;
                    else
                        chars += po.value(QStringLiteral("text")).toString().size();
                }
                hasImage = stillHasImage;
                // Never "already cleared": without an image the message is an
                // ordinary result again, and bulky text still compacts below.
                already = false;
            } else {
                const QString c = content.toString();
                chars = c.size();
                already = c.startsWith(QLatin1String("[Earlier tool result cleared"))
                       || c.startsWith(QLatin1String("[Earlier screenshot removed"));
                hasImage = ollamaImages;
            }
        } else {
            const QJsonValue v = m.value(QStringLiteral("content")).toArray().at(ref.block).toObject()
                                     .value(QStringLiteral("content"));
            if (v.isString()) {
                chars = v.toString().size();
                already = v.toString().startsWith(QLatin1String("[Earlier tool result cleared"))
                       || v.toString().startsWith(QLatin1String("[Earlier screenshot removed"));
            } else {
                for (const QJsonValue &part : v.toArray()) {
                    const QJsonObject po = part.toObject();
                    if (po.value(QStringLiteral("type")).toString() == QLatin1String("image"))
                        hasImage = true;
                    chars += po.value(QStringLiteral("text")).toString().size();
                }
            }
        }
        if (already || (!hasImage && chars < kCompactMinChars))
            continue;
        candidates.append({ ref, chars, hasImage });
        images += hasImage ? 1 : 0;
    }
    // Not worth breaking the cache for yet.
    if (candidates.size() < kClearBatchResults && images < kClearBatchImages)
        return 0;

    auto placeholder = [](int chars) {
        return QStringLiteral("[Earlier tool result cleared to save context (%1 chars). Re-run the tool if you need it again.]").arg(chars);
    };
    int cleared = 0;
    for (const Candidate &cand : candidates) {
        QJsonObject m = m_agentMessages.at(cand.ref.msg).toObject();
        if (cand.ref.block < 0) {
            if (cand.image) {
                // Drop the image but keep the text: the model still sees what
                // the tool reported, just not the pixels.
                const QJsonValue content = m.value(QStringLiteral("content"));
                if (content.isArray()) {
                    QJsonArray parts;
                    for (const QJsonValue &part : content.toArray()) {
                        if (part.toObject().value(QStringLiteral("type")).toString()
                                == QLatin1String("image_url"))
                            continue;
                        parts.append(part);
                    }
                    m[QStringLiteral("content")] = parts;
                } else {
                    m[QStringLiteral("content")] = QStringLiteral("[Earlier screenshot removed to save context.]");
                }
                m.remove(QStringLiteral("images"));
            } else {
                m[QStringLiteral("content")] = placeholder(cand.chars);
            }
        } else {
            QJsonArray content = m.value(QStringLiteral("content")).toArray();
            QJsonObject block = content.at(cand.ref.block).toObject();
            const QJsonValue bc = block.value(QStringLiteral("content"));
            if (cand.image && bc.isArray()) {
                // Anthropic tool results pair text with the screenshot
                // ([{type:"text",...},{type:"image",...}]): drop only the
                // image parts so the model keeps what the tool reported, but
                // note the removal so it doesn't wonder where the image went.
                QJsonArray parts;
                bool droppedImage = false;
                for (const QJsonValue &part : bc.toArray()) {
                    if (part.toObject().value(QStringLiteral("type")).toString()
                            == QLatin1String("image")) {
                        droppedImage = true;
                        continue;
                    }
                    parts.append(part);
                }
                if (droppedImage) {
                    parts.append(QJsonObject{
                        { QStringLiteral("type"), QStringLiteral("text") },
                        { QStringLiteral("text"),
                          QStringLiteral("[Earlier screenshot removed to save context.]") }
                    });
                }
                block[QStringLiteral("content")] = QJsonValue(parts);
            } else {
                block[QStringLiteral("content")] = cand.image
                    ? QStringLiteral("[Earlier screenshot removed to save context.]")
                    : placeholder(cand.chars);
            }
            content[cand.ref.block] = block;
            m[QStringLiteral("content")] = content;
        }
        m_agentMessages[cand.ref.msg] = m;
        ++cleared;
    }
    m_compactedResults += cleared;
    // The cached prefix just changed; the old tail marker points at nothing useful.
    m_lastCacheMark = -1;
    return cleared;
}
