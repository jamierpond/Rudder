#pragma once

#include "Items.h"
#include "LineEdit.h"

#include <eacp/UI/UI.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rudder
{
// What the switcher draws. Filtering, ranking, selection and the live preview
// are the app's; the panel only paints this and reports input.
struct PanelModel
{
    std::vector<RudderItem> items;
    int selected = 0;

    // The selection's window titles, front first, and whether its front window
    // was actually raised behind the panel.
    std::vector<std::string> previewWindows;
    bool previewRaised = false;
};

struct SettingsModel
{
    std::string summonLabel;
    std::string backLabel;
    std::string quitLabel;
    std::string favouriteLabel;
    std::string renameLabel;

    // Canonical chord text: the panel matches these itself.
    std::string quit;
    std::string favourite;
    std::string rename;

    // Where the favourites file (~/.config/rudder.json) lives, for anyone (or any agent) editing it.
    std::string favouritesPath;

    bool accessibilityTrusted = false;
    std::string error;
};

using IconLookup =
    std::function<const eacp::Graphics::Image&(const std::string& appId)>;

// The rows: icon, title, subtitle, selection highlight. Painted by hand rather
// than out of ListBox so a row is exactly the shape the panel wants, and only
// the list repaints when the selection moves.
class ResultsList final : public eacp::UI::Component
{
public:
    ResultsList();

    void setItems(const std::vector<RudderItem>& newItems, int newSelected);

    // The selected row's title as an editor (a rename), or back to the title.
    // allSelected: the whole title is highlighted, to be replaced by the
    // first key.
    void setEditing(const std::optional<Line>& line, bool allSelected = false);

    void paint(eacp::UI::Graphics& g) override;
    void mouseDown(const eacp::UI::MouseEvent& event) override;
    bool mouseWheelMove(const eacp::UI::MouseEvent& event) override;

    eacp::UI::ImageRef iconRef(const std::string& appId);

    IconLookup iconFor;
    std::function<void(int)> onChoose = [](int) {};

    static constexpr auto rowHeight = 40.f;

private:
    void scrollToSelection();

    std::vector<RudderItem> items;
    int selected = 0;
    float scroll = 0.f;
    std::optional<Line> editing;
    bool editingAllSelected = false;
    // How far the edited title is scrolled left, in points.
    float editScroll = 0.f;

    // Textures by app id: the host's image cache hashes pixels on every get,
    // which is worth doing once per icon rather than once per paint.
    std::map<std::string, eacp::UI::ImageRef> textures;
};

// What Enter will land on: the big icon, the name, the window titles.
class PreviewPane final : public eacp::UI::Component
{
public:
    PreviewPane();

    void setPreview(const RudderItem* item,
                    std::vector<std::string> windows,
                    bool raised,
                    bool trusted);

    void paint(eacp::UI::Graphics& g) override;
    void resized() override;

    ResultsList* icons = nullptr;
    eacp::UI::Button grant {"Allow Accessibility for live preview"};

private:
    std::string title;
    std::string appId;
    std::vector<std::string> windows;
    bool raised = false;
};

class SwitcherPage final : public eacp::UI::Component
{
public:
    SwitcherPage();

    void paint(eacp::UI::Graphics& g) override;
    void resized() override;

    eacp::UI::TextEditor query;
    ResultsList list;
    PreviewPane preview;
    eacp::UI::Label footer;
};

class SettingsPage final : public eacp::UI::Component
{
public:
    SettingsPage();

    void paint(eacp::UI::Graphics& g) override;
    void resized() override;

    eacp::UI::Label title {"Rudder settings"};
    eacp::UI::Label summonCaption {"Open the switcher"};
    eacp::UI::Button summonChord;
    eacp::UI::Label backCaption {"Go back to the previous app"};
    eacp::UI::Button backChord;
    eacp::UI::Label quitCaption {"Quit the selected app, or close the selected tab (in the panel)"};
    eacp::UI::Button quitChord;
    eacp::UI::Label favouriteCaption {"Favourite / unfavourite the selection"};
    eacp::UI::Button favouriteChord;
    eacp::UI::Label renameCaption {"Rename the selected pane or tab"};
    eacp::UI::Button renameChord;
    eacp::UI::Label favouritesPath;
    eacp::UI::Label error;
    eacp::UI::Label accessibilityCaption {"Live preview (Accessibility)"};
    eacp::UI::Button accessibility {"Allow…"};
    eacp::UI::Button done {"Done"};
};

// The app's icon in a dashed-looking tile: pressing it starts an OS drag of
// Rudder.app, which is what the Accessibility list accepts.
class DragTile final : public eacp::UI::Component
{
public:
    DragTile();

    void paint(eacp::UI::Graphics& g) override;
    void mouseDown(const eacp::UI::MouseEvent& event) override;

    std::function<void()> onDragStart = [] {};

private:
    eacp::UI::ImageRef icon;
};

// Shown over the switcher while Accessibility isn't granted (tamber-web's
// AccessibilityPermissionGate, drawn natively): a translucent cover, a card
// explaining the grant, the draggable app icon and a button for the pane.
class AccessibilityOverlay final : public eacp::UI::Component
{
public:
    AccessibilityOverlay();

    void paint(eacp::UI::Graphics& g) override;
    void resized() override;

    DragTile tile;
    eacp::UI::Button openSettings {"Open Accessibility settings"};

    // Hidden where there are no grants to reset (Windows).
    eacp::UI::Button resetPermissions {"Reset permissions"};

private:
    eacp::Graphics::Rect card() const;
};

class PanelRoot final : public eacp::UI::Component
{
public:
    PanelRoot();

    void paint(eacp::UI::Graphics& g) override;
    void resized() override;

    SwitcherPage switcher;
    SettingsPage settings;
    AccessibilityOverlay overlay;
};

// The panel's content view: one ComponentHost (a GPUView) drawing the whole
// panel. Rendering is on demand, so a hidden or settled panel draws nothing.
class PanelView final : public eacp::UI::ComponentHost
{
public:
    PanelView();

    // A fresh summon: empty query, focus in it, the switcher page (or the
    // settings page) showing.
    void reset(bool showSettings);
    bool settingsOpen() const { return root.settings.isVisible(); }

    // The unlock overlay over everything, while the grant is missing.
    void setAccessibilityGate(bool show);
    bool accessibilityGateShown() const { return root.overlay.isVisible(); }

    void setModel(const PanelModel& model);
    void setSettings(const SettingsModel& model);

    // The panel's own keys come first: ↑/↓ and ctrl-n/ctrl-p move, Enter
    // accepts, Esc cancels -- the text field would otherwise take the arrows.
    // While a chord is being recorded every key goes to the recorder.
    void keyDown(const eacp::Graphics::KeyEvent& event) override;

    // After the host has drawn a frame. For latency tracing only.
    void render(eacp::GPU::Frame& frame) override;
    std::function<void()> onFrame = [] {};

    // The uptime (seconds) of the last key event that reached the panel, for
    // keystroke-to-frame tracing.
    double lastKeyTime = 0.0;

    IconLookup iconFor;

    std::function<void(const std::string&)> onQueryChange = [](auto&) {};
    std::function<void(int)> onMove = [](int) {};
    std::function<void()> onAccept = [] {};
    std::function<void(int)> onChoose = [](int) {};
    std::function<void()> onCancel = [] {};

    // The favourite binding (⌘D by default): star the selection, or unstar a
    // favourite.
    std::function<void()> onToggleFavourite = [] {};

    // The rename binding (⌃R by default): the selected row's title to edit
    // in place, or nothing when the row can't be renamed (only a provider's
    // context can). Then Enter hands back the new name (empty clears it), Esc
    // drops the edit.
    std::function<std::optional<std::string>()> onRenameStart = [] {
        return std::optional<std::string> {};
    };
    std::function<void(const std::string&)> onRename = [](auto&) {};

    // A problem with ~/.config/rudder.json, shown in the footer until it's fixed.
    void setFooterError(const std::string& error);

    // The quit binding (⌃Q by default): quit the selected app (or close the
    // selected context, for an app that can), never Rudder.
    // ⌘Q does nothing in the panel.
    std::function<void()> onQuitSelected = [] {};

    // Settings: recording starts (old hotkeys must stop firing), a chord was
    // recorded (canonical text, or empty when cancelled).
    std::function<void()> onCaptureStart = [] {};
    std::function<void(const std::string& action, const std::string& chord)>
        onCaptureEnd = [](auto&, auto&) {};
    std::function<void()> onRequestAccessibility = [] {};

    // The overlay's "Reset permissions": only offered when `can` is true.
    void setPermissionsResettable(bool can);
    std::function<void()> onResetPermissions = [] {};

    // The overlay's icon was pressed: start the bundle drag, open the pane.
    std::function<void()> onUnlockDrag = [] {};
    std::function<void()> onCloseSettings = [] {};

private:
    // fzf's emacs keys on the query field. True when the key was one.
    bool editQuery(const eacp::Graphics::KeyEvent& event);

    // A key while renaming: every key is the editor's.
    void editRename(const eacp::Graphics::KeyEvent& event);
    void stopRenaming();

    void startCapture(const std::string& action);
    void recordChord(const eacp::Graphics::KeyEvent& event);
    void showPage(bool showSettings);

    PanelRoot root;
    void updateFooter();

    SettingsModel settingsModel;
    std::string footerError;
    std::string capturing;
    std::optional<Line> renaming;
    // A rename starts with the whole title selected: typing replaces it,
    // deleting clears it, moving the cursor keeps it.
    bool renameAllSelected = false;
    bool trusted = false;
};
} // namespace rudder
