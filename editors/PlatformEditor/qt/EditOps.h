// ============================================================================
// PlatformEditor/qt/EditOps.h -- scalar edit encoding for the DOM
// ============================================================================
// std-only on purpose (no Qt types): this is core-shaped logic hosted in the
// Qt layer until the structural-writer ticket promotes it into core/ (where
// OrderedJson's emit() splice consumes the `edited' token it produces).
//
// setScalar() validates `newText' against the node's JSON kind, encodes the
// replacement token, and updates node->text so labels/validation see the new
// value immediately.  Returns false (node untouched) on an invalid literal.
// ============================================================================

#ifndef PLATEDIT_QT_EDITOPS_H
#define PLATEDIT_QT_EDITOPS_H

#include "core/OrderedJson.h"

#include <cctype>
#include <string>

namespace platedit {

inline std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", ch);
                    out += buf;
                } else out += ch;
        }
    }
    return out;
}

inline bool isJsonNumber(const std::string& s) {
    if (s.empty()) return false;
    std::size_t i = 0;
    if (s[i] == '-') ++i;
    if (i >= s.size()) return false;
    std::size_t digits = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { ++i; ++digits; }
    if (digits == 0) return false;
    if (i < s.size() && s[i] == '.') {
        ++i; digits = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { ++i; ++digits; }
        if (digits == 0) return false;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        digits = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { ++i; ++digits; }
        if (digits == 0) return false;
    }
    return i == s.size();
}

// Encode + apply an edit.  String nodes accept anything; Number/Bool/Null
// require a valid literal of that kind.
inline bool setScalar(Node* node, const std::string& newText) {
    if (!node || node->isContainer()) return false;
    std::string token;
    switch (node->kind) {
        case NodeKind::String: token = "\"" + jsonEscape(newText) + "\""; break;
        case NodeKind::Number:
            if (!isJsonNumber(newText)) return false;
            token = newText; break;
        case NodeKind::Bool:
            if (newText != "true" && newText != "false") return false;
            token = newText; break;
        case NodeKind::Null:
            if (newText != "null") return false;
            token = newText; break;
        default: return false;
    }
    if (newText == node->text && !node->dirty) return true;   // no-op
    node->text   = newText;
    node->edited = token;
    node->dirty  = true;
    return true;
}

} // namespace platedit

#endif // PLATEDIT_QT_EDITOPS_H
