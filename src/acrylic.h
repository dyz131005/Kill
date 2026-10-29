#pragma once

#include <QWidget>
#include <QColor>

// Applies a modern "acrylic" / glass backdrop to a top-level window on
// Windows 10 & 11:
//   - Windows 11: DWM system backdrop (Mica / Acrylic) + rounded corners.
//   - Windows 10: SetWindowCompositionAttribute ACCENT_ENABLE_ACRYLICBLURBEHIND.
// Returns true if a backdrop was successfully applied.
namespace Acrylic {

bool enable(QWidget *window, const QColor &tint, bool darkMode);

// Enable rounded window corners (Windows 11). No-op on older systems.
void enableRoundedCorners(QWidget *window);

// Update the dark/light mode for the window frame (title bar) so the system
// caption buttons match the app theme.
void setDarkTitleBar(QWidget *window, bool dark);

} // namespace Acrylic
