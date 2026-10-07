#include "Editor/EditorTheme.h"
#include <QApplication>
#include <QColor>
#include <QPalette>

void ApplyDarkTheme(QApplication& app)
{
    const auto& theme = DarkTheme();
    app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, theme.window);
    palette.setColor(QPalette::WindowText, theme.text);
    palette.setColor(QPalette::Base, theme.base);
    palette.setColor(QPalette::AlternateBase, theme.alternateBase);
    palette.setColor(QPalette::Text, theme.text);
    palette.setColor(QPalette::Button, theme.button);
    palette.setColor(QPalette::ButtonText, theme.text);
    palette.setColor(QPalette::Light, theme.borderStrong);
    palette.setColor(QPalette::Midlight, theme.controlHover);
    palette.setColor(QPalette::Mid, theme.panel);
    palette.setColor(QPalette::Dark, theme.separator);
    palette.setColor(QPalette::Shadow, theme.shadow);
    palette.setColor(QPalette::Highlight, theme.selection);
    palette.setColor(QPalette::HighlightedText, theme.selectionText);
    palette.setColor(QPalette::ToolTipBase, theme.panel);
    palette.setColor(QPalette::ToolTipText, theme.text);
    palette.setColor(QPalette::PlaceholderText, theme.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.disabledText);
    app.setPalette(palette);
    app.setStyleSheet(
        QStringLiteral(R"(QDockWidget::title { padding: 8px 10px; background: %1; color: %2; })" "\n")
            .arg(theme.panel.name(), theme.text.name())
        + QStringLiteral(R"(QToolBar { spacing: 6px; padding: 5px 8px; background: %1; border: 0; })" "\n")
            .arg(theme.toolbar.name())
        + QStringLiteral(R"(QToolBar#PlaybackToolbar { border-bottom: 1px solid %1; padding: 6px; })" "\n")
            .arg(theme.separator.name())
        + QStringLiteral(R"(QToolBar#SceneViewToolbar { background: %1; padding: 4px 10px; border-bottom: 1px solid %2; })" "\n")
            .arg(theme.sceneToolbar.name(), theme.separator.name())
        + QStringLiteral(R"(QToolButton { padding: 4px 8px; border: 1px solid transparent; border-radius: 3px; })" "\n")
        + QStringLiteral(R"(QToolButton:hover { background: %1; border-color: %2; })" "\n")
            .arg(theme.controlHover.name(), theme.borderStrong.name())
        + QStringLiteral(R"(QToolButton:checked { background: %1; border-color: %2; })" "\n")
            .arg(theme.checkedControl.name(), theme.checkedBorder.name())
        + QStringLiteral(R"(QMenuBar { padding: 3px 6px; background: %1; })" "\n")
            .arg(theme.panelAlt.name())
        + QStringLiteral(R"(QMenu { padding: 4px; border: 1px solid %1; })" "\n")
            .arg(theme.border.name())
        + QStringLiteral(R"(QTabBar { qproperty-drawBase: false; background: %1; })" "\n")
            .arg(theme.tabSurface.name())
        + QStringLiteral(R"(QTabBar::tab { background: %1; color: %2; padding: 8px 18px; border: 0; border-right: 1px solid %3; })" "\n")
            .arg(theme.panelAlt.name(), theme.mutedText.name(), theme.separator.name())
        + QStringLiteral(R"(QTabBar::tab:selected { background: %1; color: %2; border-bottom: 2px solid %3; })" "\n")
            .arg(theme.selectedPanel.name(), theme.strongText.name(), theme.accent.name())
        + QStringLiteral(R"(QTabBar::tab:!selected:hover { background: %1; color: %2; })" "\n")
            .arg(theme.hoveredPanel.name(), theme.text.name())
        + QStringLiteral(R"(QTabWidget::pane { border: 0; })" "\n")
        + QStringLiteral(R"(QMainWindow::separator { width: 5px; height: 5px; background: %1; })" "\n")
            .arg(theme.separator.name())
        + QStringLiteral(R"(QMainWindow::separator:hover { background: %1; })" "\n")
            .arg(theme.accent.name())
        + QStringLiteral(R"(QTreeView { border: 0; padding: 4px; })" "\n")
        + QStringLiteral(R"(QTreeView::item { min-height: 24px; })" "\n")
        + QStringLiteral(R"(QHeaderView::section { background: %1; color: %2; padding: 6px 8px; border: 0; border-right: 1px solid %3; })" "\n")
            .arg(theme.panel.name(), theme.mutedText.name(), theme.separator.name())
        + QStringLiteral(R"(QLineEdit, QDoubleSpinBox, QSpinBox { background: %1; border: 1px solid %2; border-radius: 3px; padding: 4px; })" "\n")
            .arg(theme.input.name(), theme.border.name())
        + QStringLiteral(R"(QAbstractSpinBox > QLineEdit { background: transparent; border: 0; border-radius: 0; padding: 0; })" "\n")
        + QStringLiteral(R"(QDoubleSpinBox[transformField="true"]::up-button, QDoubleSpinBox[transformField="true"]::down-button { width: 0; height: 0; border: 0; padding: 0; margin: 0; })" "\n")
        + QStringLiteral(R"(QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus { border-color: %1; })" "\n")
            .arg(theme.accent.name())
        + QStringLiteral(R"(QLineEdit#ObjectName { padding: 8px; font-weight: 600; })" "\n")
        + QStringLiteral(R"(QLabel[transformAxis="X"] { color: %1; font-weight: 600; })" "\n")
            .arg(theme.axisX.name())
        + QStringLiteral(R"(QLabel[transformAxis="Y"] { color: %1; font-weight: 600; })" "\n")
            .arg(theme.axisY.name())
        + QStringLiteral(R"(QLabel[transformAxis="Z"] { color: %1; font-weight: 600; })" "\n")
            .arg(theme.axisZ.name())
        + QStringLiteral(R"(QGroupBox[componentCard="true"] { background: %1; border: 1px solid %2; border-radius: 4px; margin-top: 0; padding-top: 26px; })" "\n")
            .arg(theme.componentCard.name(), theme.border.name())
        + QStringLiteral(R"(QGroupBox[componentCard="true"]::title { subcontrol-origin: padding; subcontrol-position: top left; padding: 6px 10px; color: %1; font-weight: 600; })" "\n")
            .arg(theme.strongText.name())
        + QStringLiteral(R"(QGroupBox[componentIcon="true"]::title { padding-left: 32px; })" "\n")
        + QStringLiteral(R"(QPlainTextEdit { background: %1; border: 1px solid %2; padding: 7px; })" "\n")
            .arg(theme.textArea.name(), theme.border.name())
        + QStringLiteral(R"(QPlainTextEdit#ConsoleOutput { border: 0; })" "\n")
        + QStringLiteral(R"(QPlainTextEdit#CommentText { border-radius: 3px; })" "\n")
        + QStringLiteral(R"(QPushButton { padding: 5px 12px; background: %1; border: 1px solid %2; border-radius: 3px; })" "\n")
            .arg(theme.button.name(), theme.borderStrong.name())
        + QStringLiteral(R"(QPushButton:hover { background: %1; border-color: %2; })" "\n")
            .arg(theme.buttonHover.name(), theme.borderHover.name())
        + QStringLiteral(R"(QPushButton#AddComponentButton { background: %1; border-color: %2; color: %3; font-weight: 600; })" "\n")
            .arg(theme.primaryButton.name(), theme.primaryBorder.name(), theme.strongText.name())
        + QStringLiteral(R"(QPushButton#AddComponentButton:hover { background: %1; })" "\n")
            .arg(theme.primaryHover.name())
        + QStringLiteral(R"(QPushButton#AddComponentButton:disabled { background: %1; border-color: %2; color: %3; })" "\n")
            .arg(theme.primaryDisabled.name(), theme.checkedBorder.name(), theme.mutedText.name())
        + QStringLiteral(R"(QPushButton:disabled { background: %1; border-color: %2; color: %3; })" "\n")
            .arg(theme.buttonDisabled.name(), theme.border.name(), theme.disabledText.name())
        + QStringLiteral(R"(QLabel[muted="true"] { color: %1; })" "\n")
            .arg(theme.mutedText.name())
        + QStringLiteral(R"(QLabel#GamePreviewTitle { color: %1; font-size: 18px; font-weight: 600; })" "\n")
            .arg(theme.text.name())
        + QStringLiteral(R"(QStatusBar { background: %1; color: %2; border-top: 1px solid %3; })" "\n")
            .arg(theme.tabSurface.name(), theme.mutedText.name(), theme.separator.name()));
}
