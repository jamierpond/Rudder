#pragma once

#include "Items.h"

#include <eacp/UI/UI.h>

#include <functional>
#include <map>
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
    eacp::UI::Label error;
    eacp::UI::Label accessibilityCaption {"Live preview (Accessibility)"};
    eacp::UI::Button accessibility {"Allow…"};
    eacp::UI::Button done {"Done"};
};

class PanelRoot final : public eacp::UI::Component
{
public:
    PanelRoot();

    void paint(eacp::UI::Graphics& g) override;
    void resized() override;

    SwitcherPage switcher;
    SettingsPage settings;
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

    void setModel(const PanelModel& model);
    void setSettings(const SettingsModel& model);

    // The panel's own keys come first: ↑/↓ and ctrl-n/ctrl-p move, Enter
    // accepts, Esc cancels -- the text field would otherwise take the arrows.
    // While a chord is being recorded every key goes to the recorder.
    void keyDown(const eacp::Graphics::KeyEvent& event) override;

    IconLookup iconFor;

    std::function<void(const std::string&)> onQueryChange = [](auto&) {};
    std::function<void(int)> onMove = [](int) {};
    std::function<void()> onAccept = [] {};
    std::function<void(int)> onChoose = [](int) {};
    std::function<void()> onCancel = [] {};

    // Settings: recording starts (old hotkeys must stop firing), a chord was
    // recorded (canonical text, or empty when cancelled).
    std::function<void()> onCaptureStart = [] {};
    std::function<void(const std::string& action, const std::string& chord)>
        onCaptureEnd = [](auto&, auto&) {};
    std::function<void()> onRequestAccessibility = [] {};
    std::function<void()> onCloseSettings = [] {};

private:
    void startCapture(const std::string& action);
    void recordChord(const eacp::Graphics::KeyEvent& event);
    void showPage(bool showSettings);

    PanelRoot root;
    SettingsModel settingsModel;
    std::string capturing;
    bool trusted = false;
};
} // namespace rudder
