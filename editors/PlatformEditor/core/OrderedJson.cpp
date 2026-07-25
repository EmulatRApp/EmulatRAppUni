// ============================================================================
// PlatformEditor/core/OrderedJson.cpp -- see OrderedJson.h
// ============================================================================
#include "OrderedJson.h"

#include <cstdio>
#include <stdexcept>

namespace platedit {

// ---- Node navigation -------------------------------------------------------

std::string Node::path() const
{
    if (parent == nullptr) return "$";
    std::string p = parent->path();
    if (parent->kind == NodeKind::Array)
        return p + "[" + std::to_string(originIndex) + "]";
    return p + "." + key;
}

Node* Node::member(const std::string& k)
{
    if (kind != NodeKind::Object) return nullptr;
    for (auto& c : children) if (c->key == k) return c.get();
    return nullptr;
}
const Node* Node::member(const std::string& k) const
{
    return const_cast<Node*>(this)->member(k);
}
Node* Node::elem(std::size_t i)
{
    if (kind != NodeKind::Array || i >= children.size()) return nullptr;
    return children[i].get();
}
const Node* Node::elem(std::size_t i) const
{
    return const_cast<Node*>(this)->elem(i);
}

// ---- Parser (hand-rolled recursive descent, authored order) ----------------
namespace {

// Thrown internally on malformed input; caught in Document::parse and turned
// into a ParseError (the tool build has exceptions enabled).
struct ParseFail {
    std::string message;
    std::size_t offset;
};

class Parser {
public:
    explicit Parser(const std::string& src) : s_(src) {}

    std::unique_ptr<Node> parseDocument()
    {
        skipWs();
        std::unique_ptr<Node> root = parseValue();
        skipWs();
        if (p_ != s_.size()) fail("trailing bytes after top-level value");
        return root;
    }

private:
    const std::string& s_;
    std::size_t        p_ = 0;

    [[noreturn]] void fail(const char* what) { throw ParseFail{what, p_}; }

    char cur() const { return p_ < s_.size() ? s_[p_] : '\0'; }
    bool eof() const { return p_ >= s_.size(); }

    void skipWs()
    {
        while (p_ < s_.size()) {
            char c = s_[p_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++p_;
            else break;
        }
    }

    // Scan a JSON string literal starting at the opening quote; returns the
    // decoded content and advances p_ past the closing quote.
    std::string scanString()
    {
        if (cur() != '"') fail("expected string");
        ++p_;                                   // opening quote
        std::string out;
        while (!eof()) {
            char c = s_[p_++];
            if (c == '"') return out;           // closing quote
            if (c == '\\') {
                if (eof()) fail("unterminated escape");
                char e = s_[p_++];
                switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':                       // \uXXXX -- keep decoded value's
                                                // key role simple; store raw hex
                    if (p_ + 4 > s_.size()) fail("short \\u escape");
                    out += '\\'; out += 'u';
                    out += s_.substr(p_, 4);
                    p_ += 4;
                    break;
                default: fail("bad escape");
                }
            } else {
                out += c;
            }
        }
        fail("unterminated string");
    }

    void scanStringSpan()                       // advance past a string literal
    {
        if (cur() != '"') fail("expected string");
        ++p_;
        while (!eof()) {
            char c = s_[p_++];
            if (c == '"') return;
            if (c == '\\') { if (eof()) fail("unterminated escape"); ++p_; }
        }
        fail("unterminated string");
    }

    void scanNumberSpan()
    {
        std::size_t start = p_;
        while (!eof()) {
            char c = s_[p_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' ||
                c == '.' || c == 'e' || c == 'E') ++p_;
            else break;
        }
        if (p_ == start) fail("expected number");
    }

    void expectLiteral(const char* lit)
    {
        for (const char* q = lit; *q; ++q) {
            if (cur() != *q) fail("expected literal");
            ++p_;
        }
    }

    std::unique_ptr<Node> parseValue()
    {
        skipWs();
        auto n = std::make_unique<Node>();
        n->begin = p_;
        char c = cur();

        if (c == '{') { n->kind = NodeKind::Object; parseObject(*n); }
        else if (c == '[') { n->kind = NodeKind::Array;  parseArray(*n); }
        else if (c == '"') { n->kind = NodeKind::String; n->text = scanString(); }
        else if (c == 't') { n->kind = NodeKind::Bool;   expectLiteral("true"); }
        else if (c == 'f') { n->kind = NodeKind::Bool;   expectLiteral("false"); }
        else if (c == 'n') { n->kind = NodeKind::Null;   expectLiteral("null"); }
        else if (c == '-' || (c >= '0' && c <= '9')) {
            n->kind = NodeKind::Number; scanNumberSpan();
        } else {
            fail("unexpected character");
        }

        n->end = p_;
        // For non-string scalars, the decoded text is the literal itself. (String
        // text was decoded above; containers keep an empty text.)
        if (n->kind == NodeKind::Number || n->kind == NodeKind::Bool ||
            n->kind == NodeKind::Null)
            n->text = s_.substr(n->begin, n->end - n->begin);
        return n;
    }

    void parseObject(Node& obj)
    {
        ++p_;                                   // consume '{'
        skipWs();
        if (cur() == '}') { ++p_; return; }     // empty object
        int idx = 0;
        for (;;) {
            skipWs();
            std::string key = scanString();     // member key
            skipWs();
            if (cur() != ':') fail("expected ':'");
            ++p_;
            std::unique_ptr<Node> child = parseValue();
            child->parent      = &obj;
            child->key         = std::move(key);
            child->originIndex = idx++;
            obj.children.push_back(std::move(child));
            skipWs();
            char c = cur();
            if (c == ',') { ++p_; continue; }
            if (c == '}') { ++p_; break; }
            fail("expected ',' or '}'");
        }
    }

    void parseArray(Node& arr)
    {
        ++p_;                                   // consume '['
        skipWs();
        if (cur() == ']') { ++p_; return; }     // empty array
        int idx = 0;
        for (;;) {
            std::unique_ptr<Node> child = parseValue();
            child->parent      = &arr;
            child->originIndex = idx++;
            arr.children.push_back(std::move(child));
            skipWs();
            char c = cur();
            if (c == ',') { ++p_; continue; }
            if (c == ']') { ++p_; break; }
            fail("expected ',' or ']'");
        }
    }
};

// JSON string escaping for regenerated tokens.  NOTE: parser stores \uXXXX
// escapes RAW in text; manifests are ASCII(128) by project rule, so that case
// does not arise in regenerated content.
std::string escapeString(const std::string& s)
{
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

void emitNode(const std::string& src, const Node* n, std::string& out, int depth);

// Regenerate a container's text (structural edit or synthetic subtree):
// 2-space indent per level, one member/element per line.  Clean descendants
// still emit via their source spans inside this.
void regenNode(const std::string& src, const Node* n, std::string& out, int depth)
{
    const std::string ind(static_cast<std::size_t>(depth) * 2, ' ');
    const std::string ind2(static_cast<std::size_t>(depth + 1) * 2, ' ');
    const char open  = (n->kind == NodeKind::Object) ? '{' : '[';
    const char close = (n->kind == NodeKind::Object) ? '}' : ']';
    if (n->children.empty()) { out += open; out += close; return; }
    out += open;
    out += '\n';
    for (std::size_t i = 0; i < n->children.size(); ++i) {
        const Node* c = n->children[i].get();
        out += ind2;
        if (n->kind == NodeKind::Object) {
            out += '"';
            out += escapeString(c->key);
            out += "\": ";
        }
        emitNode(src, c, out, depth + 1);
        if (i + 1 < n->children.size()) out += ',';
        out += '\n';
    }
    out += ind;
    out += close;
}

// Format-preserving emit: copy the source, replacing dirty scalar spans and
// regenerating structurally-edited / synthetic containers.
void emitNode(const std::string& src, const Node* n, std::string& out, int depth)
{
    if (!n->isContainer()) {
        if (n->dirty)             out += n->edited;
        else if (n->isSynthetic()) {
            if (n->kind == NodeKind::String) {
                out += '"';
                out += escapeString(n->text);
                out += '"';
            } else out += n->text;             // number/bool/null literal
        }
        else out.append(src, n->begin, n->end - n->begin);
        return;
    }
    if (n->structDirty || n->isSynthetic()) {
        regenNode(src, n, out, depth);
        return;
    }
    std::size_t cursor = n->begin;
    for (const auto& child : n->children) {
        out.append(src, cursor, child->begin - cursor);   // gap: punctuation/keys/ws
        emitNode(src, child.get(), out, depth + 1);
        cursor = child->end;
    }
    out.append(src, cursor, n->end - cursor);              // trailing ws + close bracket
}

} // namespace

// ---- structural mutation ---------------------------------------------------

std::unique_ptr<Node> Node::make(NodeKind kind, std::string key, std::string text)
{
    auto n = std::make_unique<Node>();
    n->kind = kind;
    n->key  = std::move(key);
    n->text = std::move(text);
    return n;
}

Node* Node::insertChild(std::unique_ptr<Node> child, std::size_t at)
{
    if (!isContainer()) return nullptr;
    child->parent = this;
    Node* raw = child.get();
    if (at >= children.size()) children.push_back(std::move(child));
    else children.insert(children.begin() + static_cast<std::ptrdiff_t>(at),
                         std::move(child));
    for (std::size_t i = 0; i < children.size(); ++i)
        children[i]->originIndex = static_cast<int>(i);
    structDirty = true;
    return raw;
}

bool Node::removeChildAt(std::size_t i)
{
    if (!isContainer() || i >= children.size()) return false;
    children.erase(children.begin() + static_cast<std::ptrdiff_t>(i));
    for (std::size_t k = 0; k < children.size(); ++k)
        children[k]->originIndex = static_cast<int>(k);
    structDirty = true;
    return true;
}

int Node::indexOfChild(const Node* c) const
{
    for (std::size_t i = 0; i < children.size(); ++i)
        if (children[i].get() == c) return static_cast<int>(i);
    return -1;
}

std::unique_ptr<Node> cloneNode(const Node& n)
{
    auto out = std::make_unique<Node>();
    out->kind        = n.kind;
    out->key         = n.key;
    out->text        = n.text;
    out->begin       = n.begin;
    out->end         = n.end;
    out->originIndex = n.originIndex;
    out->dirty       = n.dirty;
    out->edited      = n.edited;
    out->structDirty = n.structDirty;
    for (const auto& c : n.children) {
        auto cc = cloneNode(*c);
        cc->parent = out.get();
        out->children.push_back(std::move(cc));
    }
    return out;
}

// ---- Document --------------------------------------------------------------

Document Document::parse(std::string source, ParseError& err)
{
    Document doc;
    doc.source_ = std::move(source);
    try {
        Parser parser(doc.source_);
        doc.root_ = parser.parseDocument();
        err = ParseError{};                     // ok
    } catch (const ParseFail& f) {
        doc.root_.reset();
        err = ParseError{false, f.message, f.offset};
    }
    return doc;
}

std::string Document::emit() const
{
    std::string out;
    if (!root_) return out;
    out.reserve(source_.size() + 16);
    out.append(source_, 0, root_->begin);                   // prefix (BOM/leading ws)
    emitNode(source_, root_.get(), out, 0);
    out.append(source_, root_->end, source_.size() - root_->end);  // suffix (trailing nl)
    return out;
}

} // namespace platedit
