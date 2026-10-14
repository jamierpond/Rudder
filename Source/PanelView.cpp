#include "PanelView.h"
#include "Icon.h"
#include "Keybinding.h"
#include "LineEdit.h"

#include <algorithm>
#include <utility>

namespace rudder
{
using namespace eacp;
using eacp::Graphics::Color;
using eacp::Graphics::Rect;

namespace
{
// The panel's palette: Wim's palette colours, so the two read as one family.
const auto panelBackground = Color(0.094f, 0.094f, 0.11f, 1.f);
const auto divider = Color(1.f, 1.f, 1.f, 0.08f);
const auto text = Color(0.95f, 0.95f, 0.96f, 1.f);
const auto dimText = Color(1.f, 1.f, 1.f, 0.45f);
const auto selection = Color(0.35f, 0.51f, 1.f, 0.30f);
const auto accent = Color(0.35f, 0.51f, 1.f, 1.f);

constexpr auto queryHeight = 50.f;
constexpr auto footerHeight = 28.f;
constexpr auto listShare = 0.58f;

const UI::Theme& theme()
{
    return UI::defaultTheme();
}
} // namespace

// --- ResultsList ---------------------------------------------------------------

ResultsList::ResultsList()
{
    setInterceptsMouseClicks(true);
}

void ResultsList::setItems(const std::vector<RudderItem>& newItems, int newSelected)
{
    items = newItems;
    selected = newSelected;
    scrollToSelection();
    repaint();
}

void ResultsList::scrollToSelection()
{
    auto top = (float) selected * rowHeight;
    auto visible = std::max(getHeight() - 12.f, rowHeight);

    if (top < scroll)
        scroll = top;
    else if (top + rowHeight > scroll + visible)
        scroll = top + rowHeight - visible;

    auto maxScroll = std::max(0.f, (float) items.size() * rowHeight - visible);
    scroll = std::clamp(scroll, 0.f, maxScroll);
}

UI::ImageRef ResultsList::iconRef(const std::string& appId)
{
    if (auto found = textures.find(appId); found != textures.end())
        return found->second;

    auto* host = getHost();

    if (host == nullptr || !iconFor)
        return nullptr;

    auto& image = iconFor(appId);
    auto ref = image.isValid() ? host->getImageCache().get(image) : nullptr;
    textures[appId] = ref;
    return ref;
}

void ResultsList::paint(UI::Graphics& g)
{
    auto width = getWidth();

    if (items.empty())
    {
        g.setColour(dimText);
        g.drawText("No running app matches", {18.f, 10.f, width - 36.f, rowHeight});
        return;
    }

    for (auto index = 0; index < (int) items.size(); ++index)
    {
        auto y = 6.f + (float) index * rowHeight - scroll;

        if (y + rowHeight < 0.f || y > getHeight())
            continue;

        auto row = Rect {6.f, y, width - 12.f, rowHeight};
        auto& item = items[(std::size_t) index];

        if (index == selected)
        {
            g.setColour(selection);
            g.fillRoundedRect(row, 7.f);
        }

        // A closed app reads quieter: half-faded icon and title.
        if (auto icon = iconRef(item.appId))
            g.drawImage(icon, {row.x + 8.f, y + 6.f, 28.f, 28.f}, item.running ? 1.f : 0.55f);

        auto textArea = Rect {row.x + 46.f, y, row.w - 54.f, rowHeight};

        g.setColour(item.running ? text : Color(1.f, 1.f, 1.f, 0.7f));
        g.setFontSize(14.f);

        if (item.subtitle.empty())
            g.drawText(item.title, textArea);
        else
        {
            g.drawText(item.title, textArea.withHeight(rowHeight * 0.62f));
            g.setColour(dimText);
            g.setFontSize(11.f);
            g.drawText(item.subtitle,
                       {textArea.x, y + rowHeight * 0.5f, textArea.w, rowHeight * 0.45f});
        }
    }
}

void ResultsList::mouseDown(const UI::MouseEvent& event)
{
    auto index = (int) ((event.position.y - 6.f + scroll) / rowHeight);

    if (index >= 0 && index < (int) items.size())
        onChoose(index);
}

bool ResultsList::mouseWheelMove(const UI::MouseEvent& event)
{
    auto visible = std::max(getHeight() - 12.f, rowHeight);
    auto maxScroll = std::max(0.f, (float) items.size() * rowHeight - visible);
    auto step = event.preciseWheel ? event.wheelDelta.y : event.wheelDelta.y * rowHeight;

    scroll = std::clamp(scroll - step, 0.f, maxScroll);
    repaint();
    return true;
}

// --- PreviewPane ---------------------------------------------------------------

PreviewPane::PreviewPane()
{
    addAndMakeVisible(grant);
    grant.setVisible(false);
}

void PreviewPane::setPreview(const RudderItem* item,
                             std::vector<std::string> windowsToShow,
                             bool raisedToShow,
                             bool trusted)
{
    title = item != nullptr ? item->title : std::string {};
    appId = item != nullptr ? item->appId : std::string {};
    windows = std::move(windowsToShow);
    raised = raisedToShow;
    grant.setVisible(!trusted);
    repaint();
}

void PreviewPane::resized()
{
    grant.setBounds(getLocalBounds().inset(14.f, 0.f).fromBottom(28.f, 14.f));
}

void PreviewPane::paint(UI::Graphics& g)
{
    g.setColour(divider);
    g.fillRect({0.f, 0.f, 1.f, getHeight()});

    if (title.empty())
        return;

    auto width = getWidth();

    if (icons != nullptr)
        if (auto icon = icons->iconRef(appId))
            g.drawImage(icon, {(width - 64.f) / 2.f, 20.f, 64.f, 64.f});

    g.setColour(text);
    g.setFontSize(15.f);
    g.setFontStyle(Text::FontStyle::Bold);
    g.drawText(title, {12.f, 92.f, width - 24.f, 24.f}, UI::Justification::Centred);

    g.setFontStyle(Text::FontStyle::Regular);
    g.setFontSize(12.f);

    auto y = 128.f;
    auto shown = std::min<std::size_t>(windows.size(), 7);

    for (std::size_t i = 0; i < shown; ++i, y += 21.f)
    {
        auto front = i == 0 && raised;
        auto line = Rect {16.f, y, width - 32.f, 20.f};

        if (front)
        {
            g.setColour(accent);
            g.fillRoundedRect({line.x, y + 7.f, 6.f, 6.f}, 3.f);
        }

        g.setColour(front ? text : dimText);
        g.drawText(windows[i], line.inset(12.f, 0.f).withX(line.x + 12.f));
    }
}

// --- Pages ---------------------------------------------------------------------

SwitcherPage::SwitcherPage()
{
    auto font = Text::Font {};
    font.family = "Helvetica Neue";
    font.pointSize = 18.f;

    query.setFont(font);
    query.setDrawsFrame(false);
    query.setPlaceholder("Switch to…");
    query.setColour(text);
    query.setWantsKeyboardFocus(true);

    footer.setColour(dimText);
    footer.setFontSize(11.f);
    footer.setJustification(UI::Justification::Right);

    preview.icons = &list;

    addChildren({query, list, preview, footer});
}

void SwitcherPage::resized()
{
    auto area = getLocalBounds();

    query.setBounds(area.removeFromTop(queryHeight).inset(18.f, 10.f));
    footer.setBounds(area.removeFromBottom(footerHeight).inset(14.f, 0.f));

    list.setBounds(area.removeFromLeft(area.w * listShare));
    preview.setBounds(area);
}

void SwitcherPage::paint(UI::Graphics& g)
{
    g.setColour(divider);
    g.fillRect({0.f, queryHeight - 1.f, getWidth(), 1.f});
    g.fillRect({0.f, getHeight() - footerHeight, getWidth(), 1.f});
}

SettingsPage::SettingsPage()
{
    title.setFontSize(15.f);
    title.setFontStyle(Text::FontStyle::Bold);
    error.setColour(Color(0.88f, 0.39f, 0.36f, 1.f));
    error.setFontSize(12.f);

    for (auto* caption: {&summonCaption, &backCaption, &quitCaption, &accessibilityCaption})
        caption->setColour(text);

    addChildren({title,
                 summonCaption,
                 summonChord,
                 backCaption,
                 backChord,
                 quitCaption,
                 quitChord,
                 error,
                 accessibilityCaption,
                 accessibility,
                 done});
}

void SettingsPage::resized()
{
    auto area = getLocalBounds().inset(18.f, 0.f);

    title.setBounds(area.removeFromTop(queryHeight));
    area.removeFromTop(8.f);

    auto row = [&](UI::Label& caption, UI::Button& control)
    {
        auto line = area.removeFromTop(40.f);
        control.setBounds(line.removeFromRight(160.f).inset(0.f, 6.f));
        caption.setBounds(line);
    };

    row(summonCaption, summonChord);
    row(backCaption, backChord);
    row(quitCaption, quitChord);
    error.setBounds(area.removeFromTop(24.f));
    row(accessibilityCaption, accessibility);

    done.setBounds(area.removeFromBottom(46.f).removeFromRight(90.f).inset(0.f, 9.f));
}

void SettingsPage::paint(UI::Graphics& g)
{
    g.setColour(divider);
    g.fillRect({0.f, queryHeight - 1.f, getWidth(), 1.f});
}

DragTile::DragTile()
{
    setInterceptsMouseClicks(true);
}

void DragTile::paint(UI::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setColour(Color(1.f, 1.f, 1.f, 0.04f));
    g.fillRoundedRect(bounds, 14.f);
    g.setColour(Color(0.39f, 0.83f, 0.49f, 0.8f));
    g.drawRoundedRect(bounds.inset(0.5f), 14.f, 1.f);

    if (icon == nullptr)
        if (auto* host = getHost())
            icon = host->getImageCache().get(makeAppIcon(128));

    if (icon != nullptr)
        g.drawImage(icon, {(bounds.w - 64.f) / 2.f, 14.f, 64.f, 64.f});

    g.setColour(text);
    g.setFontSize(12.f);
    g.drawText("Drag Rudder into the list",
               {0.f, 84.f, bounds.w, 20.f},
               UI::Justification::Centred);
}

void DragTile::mouseDown(const UI::MouseEvent&)
{
    onDragStart();
}

AccessibilityOverlay::AccessibilityOverlay()
{
    setInterceptsMouseClicks(true);
    addChildren({tile, openSettings});
}

Rect AccessibilityOverlay::card() const
{
    auto bounds = getLocalBounds();
    return {(bounds.w - 500.f) / 2.f, (bounds.h - 364.f) / 2.f, 500.f, 364.f};
}

void AccessibilityOverlay::resized()
{
    auto area = card();
    tile.setBounds({area.x + (area.w - 220.f) / 2.f, area.y + 92.f, 220.f, 112.f});
    openSettings.setBounds(
        {area.x + (area.w - 220.f) / 2.f, area.bottom() - 52.f, 220.f, 28.f});
}

void AccessibilityOverlay::paint(UI::Graphics& g)
{
    g.fillAll(Color(0.f, 0.f, 0.f, 0.5f));

    auto area = card();
    g.setColour(Color(0.15f, 0.15f, 0.18f, 1.f));
    g.fillRoundedRect(area, 22.f);

    auto line = [&](const char* words, float y, float size, Color colour)
    {
        g.setFontSize(size);
        g.setColour(colour);
        g.drawText(words, {area.x, area.y + y, area.w, size + 6.f}, UI::Justification::Centred);
    };

    g.setFontStyle(Text::FontStyle::Bold);
    line("Unlock Rudder", 22.f, 19.f, text);
    g.setFontStyle(Text::FontStyle::Regular);
    line("Rudder is running, but it needs macOS Accessibility", 54.f, 12.f, dimText);
    line("access to preview and switch to other apps' windows.", 70.f, 12.f, dimText);

    auto steps = area.y + 216.f;
    auto step = [&](const char* words, float y)
    {
        g.setFontSize(11.f);
        g.setColour(dimText);
        g.drawText(words, {area.x + 36.f, steps + y, area.w - 60.f, 16.f});
    };

    step("1. Start dragging the icon: System Settings opens at", 0.f);
    step("    Privacy & Security › Accessibility.", 15.f);
    step("2. Drop it into the list, or press + there and choose Rudder.", 32.f);
    step("3. Turn Rudder on. This goes away by itself.", 49.f);
}

PanelRoot::PanelRoot()
{
    // eacp components start visible, so the two that wait are hidden here.
    addChildren({switcher, settings, overlay});
    settings.setVisible(false);
    overlay.setVisible(false);
}

void PanelRoot::resized()
{
    switcher.setBounds(getLocalBounds());
    settings.setBounds(getLocalBounds());
    overlay.setBounds(getLocalBounds());
}

void PanelRoot::paint(UI::Graphics& g)
{
    g.fillAll(panelBackground);
}

// --- PanelView -----------------------------------------------------------------

PanelView::PanelView()
{
    setFontFamily("Helvetica Neue");
    setFontPointSize(13.f);
    setBackgroundColour(panelBackground);
    setTabMovesFocus(false);
    setRootComponent(root);

    auto& switcher = root.switcher;
    switcher.query.onTextChange = [this](const std::string& text) { onQueryChange(text); };
    switcher.list.onChoose = [this](int index) { onChoose(index); };
    switcher.list.iconFor = [this](const std::string& id) -> const Graphics::Image&
    { return iconFor(id); };
    switcher.preview.grant.onClick = [this] { onRequestAccessibility(); };

    auto& settings = root.settings;
    settings.summonChord.onClick = [this] { startCapture("summon"); };
    settings.backChord.onClick = [this] { startCapture("back"); };
    settings.quitChord.onClick = [this] { startCapture("quit"); };
    settings.accessibility.onClick = [this] { onRequestAccessibility(); };
    settings.done.onClick = [this] { onCloseSettings(); };

    root.overlay.tile.onDragStart = [this] { onUnlockDrag(); };
    root.overlay.openSettings.onClick = [this] { onRequestAccessibility(); };
}

void PanelView::setAccessibilityGate(bool show)
{
    if (root.overlay.isVisible() != show)
        root.overlay.setVisible(show);

    // Typing into the switcher behind the cover would preview nothing.
    if (show)
        root.switcher.query.giveAwayKeyboardFocus();
    else if (!settingsOpen())
        root.switcher.query.grabKeyboardFocus();
}

void PanelView::reset(bool showSettings)
{
    capturing.clear();
    root.switcher.query.setText({});
    showPage(showSettings);
}

void PanelView::showPage(bool showSettings)
{
    root.switcher.setVisible(!showSettings);
    root.settings.setVisible(showSettings);

    if (showSettings)
        root.switcher.query.giveAwayKeyboardFocus();
    else
        root.switcher.query.grabKeyboardFocus();
}

void PanelView::setModel(const PanelModel& model)
{
    auto& switcher = root.switcher;
    switcher.list.setItems(model.items, model.selected);

    auto* item = model.selected < (int) model.items.size()
                     ? &model.items[(std::size_t) model.selected]
                     : nullptr;

    switcher.preview.setPreview(
        item, model.previewWindows, model.previewRaised, trusted);
}

void PanelView::setSettings(const SettingsModel& model)
{
    settingsModel = model;
    trusted = model.accessibilityTrusted;

    auto& settings = root.settings;
    settings.summonChord.setText(capturing == "summon" ? "Press keys…"
                                                       : model.summonLabel);
    settings.backChord.setText(capturing == "back" ? "Press keys…" : model.backLabel);
    settings.quitChord.setText(capturing == "quit" ? "Press keys…" : model.quitLabel);
    settings.error.setText(model.error);
    settings.accessibility.setText(model.accessibilityTrusted ? "Allowed" : "Allow…");

    root.switcher.footer.setText(model.summonLabel + " switch   ·   " + model.backLabel
                                 + " back   ·   " + model.quitLabel + " quit app");
    root.switcher.preview.grant.setVisible(!trusted);

    if (!settingsOpen())
        root.switcher.query.grabKeyboardFocus();
}

void PanelView::startCapture(const std::string& action)
{
    capturing = action;
    onCaptureStart();
    setSettings(settingsModel);
}

void PanelView::recordChord(const Graphics::KeyEvent& event)
{
    auto action = capturing;

    if (event.keyCode == Graphics::KeyCode::Escape)
    {
        capturing.clear();
        onCaptureEnd(action, {});
        setSettings(settingsModel);
        return;
    }

    auto key = keyNameFor(event.keyCode);

    // A lone modifier press reports no usable key; keep listening.
    if (!key)
        return;

    auto chord = Chord {};
    chord.control = event.modifiers.control;
    chord.option = event.modifiers.alt;
    chord.shift = event.modifiers.shift;
    chord.command = event.modifiers.command;
    chord.key = *key;

    capturing.clear();
    onCaptureEnd(action, serialiseChord(chord));
}

void PanelView::render(GPU::Frame& frame)
{
    ComponentHost::render(frame);
    onFrame();
}

bool PanelView::editQuery(const Graphics::KeyEvent& event)
{
    using namespace Graphics;

    auto& query = root.switcher.query;
    auto& mods = event.modifiers;

    if (mods.command || mods.shift)
        return false;

    auto current = Line {query.getText(), query.getCaretPosition()};
    auto edited = std::optional<Line> {};

    if (mods.control && !mods.alt)
    {
        switch (event.keyCode)
        {
            case KeyCode::W: edited = line::deleteWhitespaceWord(current); break;
            case KeyCode::U: edited = line::deleteToStart(current); break;
            case KeyCode::K: edited = line::deleteToEnd(current); break;
            case KeyCode::A: edited = line::start(current); break;
            case KeyCode::E: edited = line::end(current); break;
            case KeyCode::B: edited = line::left(current); break;
            case KeyCode::F: edited = line::right(current); break;
            case KeyCode::H: edited = line::backspace(current); break;
            case KeyCode::D: edited = line::deleteForward(current); break;
            default: break;
        }
    }
    else if (mods.alt && !mods.control)
    {
        switch (event.keyCode)
        {
            case KeyCode::B: edited = line::wordLeft(current); break;
            case KeyCode::F: edited = line::wordRight(current); break;
            case KeyCode::Delete: edited = line::deleteWord(current); break;
            default: break;
        }
    }

    if (!edited)
        return false;

    // An edit re-filters and re-previews exactly as typing does.
    if (edited->text != current.text)
        query.setText(edited->text, true);

    query.setCaretPosition(edited->cursor);
    return true;
}

void PanelView::keyDown(const Graphics::KeyEvent& event)
{
    using namespace Graphics;

    lastKeyTime = event.timestamp;

    if (!capturing.empty())
    {
        recordChord(event);
        return;
    }

    auto ctrl = event.modifiers.control;
    auto key = event.keyCode;

    if (accessibilityGateShown())
    {
        if (key == KeyCode::Escape)
            onCancel();

        return;
    }

    if (settingsOpen())
    {
        if (key == KeyCode::Escape)
            onCancel();
        else
            ComponentHost::keyDown(event);

        return;
    }

    auto quit = parseChord(settingsModel.quit);
    auto& mods = event.modifiers;

    if (quit && matchesChord(*quit, key, mods.control, mods.alt, mods.shift, mods.command))
        onQuitSelected();
    else if (editQuery(event))
        return;
    else if (key == KeyCode::DownArrow || (ctrl && key == KeyCode::N))
        onMove(1);
    else if (key == KeyCode::UpArrow || (ctrl && key == KeyCode::P))
        onMove(-1);
    else if (key == KeyCode::Return)
        onAccept();
    else if (key == KeyCode::Escape)
        onCancel();
    else
        ComponentHost::keyDown(event);
}
} // namespace rudder
