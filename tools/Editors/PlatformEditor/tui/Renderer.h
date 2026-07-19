// ============================================================================
// PlatformEditor/tui/Renderer.h -- compose a TUI frame as a string
// ============================================================================
// Qt-free, TTY-free: renderFrame() returns the full screen as a string, so it is
// unit-testable without a terminal. The interactive shell (main.cpp) prints it;
// tests assert on it. ASCII-128 box drawing only (house style / V-13).
// ============================================================================

#ifndef PLATEDIT_RENDERER_H
#define PLATEDIT_RENDERER_H

#include "core/ManifestView.h"

#include <string>
#include <vector>

namespace platedit {

enum class Pane { Tree, Props };

struct FrameState {
    std::string              title;          // top bar (e.g. "PlatformEditor  ds20 *")
    const std::vector<TreeRow>*  tree  = nullptr;
    int                      treeSel   = 0;
    const std::vector<PropRow>*  props = nullptr;
    int                      propSel   = 0;
    Pane                     active    = Pane::Tree;
    std::string              status;         // bottom help/status line
    int                      width     = 80;
    int                      height    = 24;
    bool                     ansi      = false;   // wrap selected row in reverse video
};

std::string renderFrame(const FrameState& s);

} // namespace platedit

#endif // PLATEDIT_RENDERER_H
