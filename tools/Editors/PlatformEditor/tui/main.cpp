// ============================================================================
// PlatformEditor/tui/main.cpp -- terminal frontend (Qt-free)
// ============================================================================
// Master-detail manifest editor in the terminal (spec Section 8, TUI variant):
// left = container tree, right = property pane, bottom = status/help.
//
// Two modes:
//   platedit_tui <manifest> [--schema P] [--catalog C]     interactive editor
//   platedit_tui <manifest> ... --render [--cols N --rows M]  print one frame + exit
//
// --render needs no TTY, so it is how the layout is smoke-tested in CI here.
// Interactive mode uses termios raw input + an ANSI alternate screen; edits mark
// the DOM dirty and 's' writes via Document::emit() (format-preserving save).
// ============================================================================

#include "core/DeviceCatalog.h"
#include "core/ManifestView.h"
#include "core/OrderedJson.h"
#include "core/SchemaPolicy.h"
#include "tui/Renderer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace platedit;

namespace {

bool readFile(const std::string& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

bool writeFile(const std::string& p, const std::string& data)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << data;
    return f.good();
}

// A quoted-or-bare JSON token for an edited scalar of the given tier.
std::string serializeEdit(Tier tier, const std::string& input)
{
    switch (tier) {
    case Tier::Int:
    case Tier::Bool:
        return input;                            // numbers / true|false verbatim
    default:
        // everything else is a JSON string; escape " and backslash
        std::string out = "\"";
        for (char c : input) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += "\"";
        return out;
    }
}

// Shared editor state.
struct App {
    Document      doc;
    SchemaPolicy  policy;
    DeviceCatalog catalog;
    std::string   path;
    bool          dirty = false;

    TreeView*                 tree = nullptr;
    int                       treeSel = 0;
    int                       propSel = 0;
    Pane                      active = Pane::Tree;
    std::vector<PropRow>      props;      // properties of the selected tree node

    void refreshProps()
    {
        props.clear();
        const auto& rows = tree->rows();
        if (treeSel >= 0 && treeSel < static_cast<int>(rows.size())) {
            Node* n = const_cast<Node*>(rows[treeSel].node);
            props = propertiesOf(n, policy);
        }
        if (propSel >= static_cast<int>(props.size())) propSel = 0;
    }

    FrameState frame(int cols, int rows, bool ansi)
    {
        FrameState s;
        s.title   = "PlatformEditor  " + path + (dirty ? "  *" : "");
        s.tree    = &tree->rows();
        s.treeSel = treeSel;
        s.props   = &props;
        s.propSel = propSel;
        s.active  = active;
        s.status  = "[Tab] pane  [up/dn] move  [Enter] expand/edit  [s] save  [q] quit";
        s.width   = cols;
        s.height  = rows;
        s.ansi    = ansi;
        return s;
    }
};

// ---- render-only mode (no TTY) --------------------------------------------
int runRender(App& app, int cols, int rows)
{
    app.tree->expandAll();
    app.refreshProps();
    std::printf("%s\n", renderFrame(app.frame(cols, rows, /*ansi=*/false)).c_str());
    return 0;
}

#if !defined(_WIN32)
// ---- interactive mode ------------------------------------------------------
struct RawMode {
    termios saved{};
    bool ok = false;
    RawMode()
    {
        if (tcgetattr(STDIN_FILENO, &saved) != 0) return;
        termios raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        ok = (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0);
    }
    ~RawMode() { if (ok) tcsetattr(STDIN_FILENO, TCSANOW, &saved); }
};

void draw(App& app, int cols, int rows)
{
    std::string frame = renderFrame(app.frame(cols, rows, /*ansi=*/true));
    std::string out = "\x1b[H\x1b[2J" + frame;    // home + clear + frame
    (void)!write(STDOUT_FILENO, out.data(), out.size());
}

// Read one logical key. Returns a byte, or a negative code for arrows:
// -1 up, -2 down, -3 right, -4 left.
int readKey()
{
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return 'q';
    if (c != '\x1b') return static_cast<unsigned char>(c);
    char seq[2] = {0, 0};
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
        switch (seq[1]) {
        case 'A': return -1;
        case 'B': return -2;
        case 'C': return -3;
        case 'D': return -4;
        }
    }
    return '\x1b';
}

// Prompt for a line at the bottom of the screen (cooked-ish), return false on ESC.
bool promptLine(const std::string& label, std::string& out, int rows)
{
    std::string p = "\x1b[" + std::to_string(rows) + ";1H\x1b[2K" + label;
    (void)!write(STDOUT_FILENO, p.data(), p.size());
    out.clear();
    for (;;) {
        char c = 0;
        if (read(STDIN_FILENO, &c, 1) != 1) return false;
        if (c == '\r' || c == '\n') return true;
        if (c == '\x1b') return false;
        if (c == 127 || c == 8) {                 // backspace
            if (!out.empty()) { out.pop_back(); (void)!write(STDOUT_FILENO, "\b \b", 3); }
            continue;
        }
        out += c;
        (void)!write(STDOUT_FILENO, &c, 1);
    }
}

int runInteractive(App& app)
{
    winsize ws{};
    int cols = 80, rows = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        cols = ws.ws_col; rows = ws.ws_row;
    }

    app.tree->expandAll();
    app.refreshProps();

    (void)!write(STDOUT_FILENO, "\x1b[?1049h", 8);   // alternate screen
    RawMode rawGuard;

    bool running = true;
    while (running) {
        draw(app, cols, rows);
        int k = readKey();
        const auto& trows = app.tree->rows();

        if (k == 'q') {
            running = false;
        } else if (k == '\t') {
            app.active = (app.active == Pane::Tree) ? Pane::Props : Pane::Tree;
        } else if (k == -1) {                         // up
            if (app.active == Pane::Tree) { if (app.treeSel > 0) --app.treeSel; app.refreshProps(); }
            else if (app.propSel > 0) --app.propSel;
        } else if (k == -2) {                         // down
            if (app.active == Pane::Tree) {
                if (app.treeSel + 1 < static_cast<int>(trows.size())) ++app.treeSel;
                app.refreshProps();
            } else if (app.propSel + 1 < static_cast<int>(app.props.size())) ++app.propSel;
        } else if (k == 's') {
            if (writeFile(app.path, app.doc.emit())) { app.dirty = false; }
        } else if (k == '\r' || k == '\n') {
            if (app.active == Pane::Tree) {
                if (app.treeSel < static_cast<int>(trows.size())) {
                    const Node* n = trows[app.treeSel].node;
                    if (trows[app.treeSel].expandable) { app.tree->toggle(n); app.refreshProps(); }
                    else { app.active = Pane::Props; }
                }
            } else if (app.propSel < static_cast<int>(app.props.size())) {
                PropRow& pr = app.props[app.propSel];
                std::string input;
                std::string label = "edit " + pr.key + " [" + pr.value + "] = ";
                if (promptLine(label, input, rows) && pr.node) {
                    pr.node->dirty  = true;
                    pr.node->edited = serializeEdit(pr.tier, input);
                    app.dirty = true;
                    app.tree->rebuild();              // labels may have changed
                    app.refreshProps();
                }
            }
        }
        if (app.treeSel >= static_cast<int>(trows.size()))
            app.treeSel = trows.empty() ? 0 : static_cast<int>(trows.size()) - 1;
    }

    (void)!write(STDOUT_FILENO, "\x1b[?1049l", 8);   // leave alternate screen
    return 0;
}
#endif // !_WIN32

std::string argValue(int argc, char** argv, const char* flag, const std::string& dflt)
{
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
    return dflt;
}
bool hasFlag(int argc, char** argv, const char* flag)
{
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], flag) == 0) return true;
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: %s <manifest.json> [--schema P] [--catalog C] [--render [--cols N --rows M]]\n",
            argv[0]);
        return 2;
    }
    App app;
    app.path = argv[1];

    std::string mjson;
    if (!readFile(app.path, mjson)) { std::fprintf(stderr, "cannot read %s\n", app.path.c_str()); return 2; }
    ParseError perr;
    app.doc = Document::parse(mjson, perr);
    if (!perr.ok) { std::fprintf(stderr, "parse error: %s @ %zu\n", perr.message.c_str(), perr.offset); return 2; }

    std::string sjson, cjson, err;
    if (readFile(argValue(argc, argv, "--schema", "schema/platform_schema.json"), sjson))
        app.policy = SchemaPolicy::load(sjson, err);
    if (readFile(argValue(argc, argv, "--catalog", "catalog/device_catalog.json"), cjson))
        app.catalog = DeviceCatalog::load(cjson, err);

    TreeView tree(&app.doc, &app.policy, &app.catalog);
    app.tree = &tree;

    if (hasFlag(argc, argv, "--render")) {
        int cols = std::atoi(argValue(argc, argv, "--cols", "80").c_str());
        int rows = std::atoi(argValue(argc, argv, "--rows", "30").c_str());
        return runRender(app, cols, rows);
    }
#if !defined(_WIN32)
    return runInteractive(app);
#else
    std::fprintf(stderr, "interactive mode not built on this platform; use --render\n");
    return 2;
#endif
}
