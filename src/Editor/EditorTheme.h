#pragma once

#include <QColor>

class QApplication;

// Editor UI colors only. This is a fixed preset, with no theme lifecycle or plugins.
struct EditorTheme
{
    QColor window{"#24272d"};
    QColor panel{"#2f343c"};
    QColor panelAlt{"#292e35"};
    QColor base{"#1d2025"};
    QColor alternateBase{"#22262c"};
    QColor toolbar{"#2d323a"};
    QColor sceneToolbar{"#272c33"};
    QColor tabSurface{"#242a31"};
    QColor selectedPanel{"#353c46"};
    QColor hoveredPanel{"#313841"};
    QColor componentCard{"#2c3139"};
    QColor input{"#1e2228"};
    QColor textArea{"#1d2127"};
    QColor button{"#343c47"};
    QColor buttonHover{"#414d5c"};
    QColor buttonDisabled{"#2e343d"};
    QColor controlHover{"#3b434e"};
    QColor border{"#414954"};
    QColor borderStrong{"#4b5665"};
    QColor borderHover{"#66788e"};
    QColor separator{"#171a20"};
    QColor shadow{"#101419"};
    QColor text{"#e0e4eb"};
    QColor strongText{"#f0f3f7"};
    QColor mutedText{"#aab3c0"};
    QColor disabledText{"#747b86"};
    QColor icon{"#dce5f0"};
    QColor accent{"#739ac7"};
    QColor selection{"#466891"};
    QColor selectionText{Qt::white};
    QColor checkedControl{"#405f85"};
    QColor checkedBorder{"#5a7ea7"};
    QColor primaryButton{"#3d5c80"};
    QColor primaryHover{"#4a6f99"};
    QColor primaryDisabled{"#2e3947"};
    QColor primaryBorder{"#6487ae"};
    QColor info{"#a9c9f5"};
    QColor warning{"#ffd56a"};
    QColor error{"#ee6262"};
    QColor success{"#70d98b"};

    QColor axisX = error;
    QColor axisY = success;
    QColor axisZ{"#639df5"};
    QColor gizmoOutline{"#101318"};
    QColor gizmoCenter{Qt::white};
    QColor viewportBackground = QColor::fromRgbF(.075f, .085f, .105f);
    QColor viewportGridMinor = QColor::fromRgbF(.19f, .22f, .27f);
    QColor viewportGridMajor = QColor::fromRgbF(.32f, .36f, .42f);
    QColor viewportAxisX = QColor::fromRgbF(.65f, .28f, .28f);
    QColor viewportAxisY = QColor::fromRgbF(.28f, .65f, .36f);
    QColor viewportAxisZ = QColor::fromRgbF(.28f, .45f, .7f);
    QColor referenceCube = QColor::fromRgbF(.45f, .68f, .95f);

    QColor graphBackground{"#191e25"};
    QColor graphGridMinor{"#222932"};
    QColor graphGridMajor{"#2d3742"};
    QColor graphNode{"#2b333f"};
    QColor graphNodeBorder{"#526073"};
    QColor graphNodeSelected{"#9ec8f3"};
    QColor graphNodeDivider{"#465465"};
    QColor graphLabel{"#b8c9de"};
    QColor graphPinBorder{"#c4d8ef"};
    QColor graphPinHover{"#c9e4ff"};
    QColor graphConnection{"#8eb4df"};
    QColor graphConnectionSelected{"#f1c675"};
    QColor graphConnectionPreview{"#a9ccec"};
};

// One immutable preset shared by the shell and independently usable editor tools.
inline const EditorTheme& DarkTheme()
{
    static const EditorTheme theme;
    return theme;
}

void ApplyDarkTheme(QApplication& app);
