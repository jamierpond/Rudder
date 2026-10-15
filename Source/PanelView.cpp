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
// The panel's palette: Grazer's palette colours, so the two read as one family.
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

void ResultsList::setEditing(const std::optional<Line>& line, bool allSelected)
{
    if (!line)
        editScroll = 0.f;

    editing = line;
    editingAllSelected = line && allSelected;
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

        // A closed app reads quieter: half-faded icon and title. Favourites
        // without an app of their own (a url, a path) and the web row have
        // icons by row id.
        auto byId = item.kind == "web" || (item.kind == "favourite" && item.appId.empty());
        auto iconKey = byId ? item.id : item.appId;

        if (auto icon = iconRef(iconKey))
            g.drawImage(icon, {row.x + 8.f, y + 6.f, 28.f, 28.f}, item.running ? 1.f : 0.55f);
        else if (item.kind == "web")
        {
            g.setColour(dimText);
            g.setFontSize(20.f);
            g.drawText("🌐", {row.x + 8.f, y, 28.f, rowHeight}, UI::Justification::Centred);
        }

        auto textArea = Rect {row.x + 46.f, y, row.w - 74.f, rowHeight};

        if (item.kind == "favourite")
        {
            g.setColour(Color(1.f, 0.84f, 0.43f, 1.f));
            g.setFontSize(14.f);
            g.drawText("★", {row.right() - 26.f, y, 20.f, rowHeight}, UI::Justification::Centred);
        }

        g.setColour(item.running ? text : Color(1.f, 1.f, 1.f, 0.7f));
        g.setFontSize(14.f);

        auto titleArea = item.subtitle.empty() ? textArea
                                               : textArea.withHeight(rowHeight * 0.62f);
        auto renaming = editing && index == selected;
        auto& title = renaming ? editing->text : item.title;

        // Renaming: the title is the editor, framed, with a caret.
        if (renaming)
        {
            auto box = Rect {titleArea.x - 4.f,
                             titleArea.y + titleArea.h / 2.f - 10.f,
                             titleArea.w + 8.f,
                             20.f};
            g.setColour(Color(0.f, 0.f, 0.f, 0.35f));
            g.fillRoundedRect(box, 4.f);
            g.setColour(accent);
            g.drawRoundedRect(box, 4.f, 1.f);

            // A title wider than the box scrolls, so the caret and what's
            // typed at it stay in view (it starts at the end of the title).
            auto cursor = std::clamp(editing->cursor, 0, (int) title.size());
            auto caret = g.measureText(title.substr(0, (std::size_t) cursor));
            auto width = g.measureText(title);
            auto visible = titleArea.w - 2.f;
            editScroll = line::scrollToCaret(caret, width, visible, editScroll);

            auto x = titleArea.x - editScroll;

            g.saveState();
            g.reduceClipRegion({titleArea.x, box.y, titleArea.w, box.h});
            if (editingAllSelected)
            {
                g.setColour(accent.withAlpha(0.45f));
                g.fillRect({x, box.y + 3.f, width, box.h - 6.f});
            }
            else
                g.fillRect({x + caret, box.y + 3.f, 1.5f, box.h - 6.f});

            g.setColour(text);
            g.drawText(title, {x, titleArea.y, width + 4.f, titleArea.h});
            g.restoreState();
        }

        if (item.subtitle.empty())
        {
            if (!renaming)
                g.drawText(title, textArea);
        }
        else
        {
            if (!renaming)
                g.drawText(title, titleArea);

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

    for (auto* caption: {&summonCaption,
                         &backCaption,
                         &quitCaption,
                         &favouriteCaption,
                         &renameCaption,
                         &accessibilityCaption})
        caption->setColour(text);

    favouritesPath.setColour(dimText);
    favouritesPath.setFontSize(11.f);

    addChildren({title,
                 summonCaption,
                 summonChord,
                 backCaption,
                 backChord,
                 quitCaption,
                 quitChord,
                 favouriteCaption,
                 favouriteChord,
                 renameCaption,
                 renameChord,
                 favouritesPath,
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
    row(favouriteCaption, favouriteChord);
    row(renameCaption, renameChord);
    row(accessibilityCaption, accessibility);
    favouritesPath.setBounds(area.removeFromTop(22.f));
    error.setBounds(area.removeFromTop(22.f));

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
            icon = host->getImageCache().get(loadAppIcon());

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
    addChildren({tile, openSettings, resetPermissions});
}

Rect AccessibilityOverlay::card() const
{
    auto bounds = getLocalBounds();
    return {(bounds.w - 500.f) / 2.f, (bounds.h - 384.f) / 2.f, 500.f, 384.f};
}

void AccessibilityOverlay::resized()
{
    auto area = card();
    tile.setBounds({area.x + (area.w - 220.f) / 2.f, area.y + 92.f, 220.f, 112.f});
    auto buttons = resetPermissions.isVisible() ? 2.f : 1.f;
    auto left = area.x + (area.w - (220.f * buttons + 12.f * (buttons - 1.f))) / 2.f;
    openSettings.setBounds({left, area.bottom() - 52.f, 220.f, 28.f});
    resetPermissions.setBounds({left + 232.f, area.bottom() - 52.f, 220.f, 28.f});
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

    if (resetPermissions.isVisible())
        step("Already on but still locked? Reset permissions, then turn it on again.", 72.f);
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
    settings.favouriteChord.onClick = [this] { startCapture("favourite"); };
    settings.renameChord.onClick = [this] { startCapture("rename"); };
    settings.accessibility.onClick = [this] { onRequestAccessibility(); };
    settings.done.onClick = [this] { onCloseSettings(); };

    root.overlay.tile.onDragStart = [this] { onUnlockDrag(); };
    root.overlay.openSettings.onClick = [this] { onRequestAccessibility(); };
    root.overlay.resetPermissions.onClick = [this] { onResetPermissions(); };
}

void PanelView::setPermissionsResettable(bool can)
{
    root.overlay.resetPermissions.setVisible(can);
    root.overlay.resized();
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
    stopRenaming();
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
    settings.favouriteChord.setText(capturing == "favourite" ? "Press keys…"
                                                             : model.favouriteLabel);
    settings.renameChord.setText(capturing == "rename" ? "Press keys…" : model.renameLabel);
    settings.favouritesPath.setText("Favourites file: " + model.favouritesPath);
    settings.error.setText(model.error);
    settings.accessibility.setText(model.accessibilityTrusted ? "Allowed" : "Allow…");

    updateFooter();
    root.switcher.preview.grant.setVisible(!trusted);

    if (!settingsOpen())
        root.switcher.query.grabKeyboardFocus();
}

void PanelView::setFooterError(const std::string& error)
{
    footerError = error;
    updateFooter();
}

void PanelView::updateFooter()
{
    auto& footer = root.switcher.footer;
    auto& model = settingsModel;

    if (!footerError.empty())
    {
        footer.setColour(Color(0.88f, 0.39f, 0.36f, 1.f));
        footer.setText(footerError);
        return;
    }

    footer.setColour(dimText);
    footer.setText(model.summonLabel + " switch   ·   " + model.backLabel + " back   ·   "
                   + model.favouriteLabel + " favourite   ·   " + model.renameLabel
                   + " rename   ·   " + model.quitLabel + " quit app");
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

namespace
{
// fzf's emacs keys, for the query field and the rename editor alike.
std::optional<Line> emacsEdit(const Line& current, const Graphics::KeyEvent& event)
{
    using namespace Graphics;

    auto& mods = event.modifiers;

    if (mods.shift)
        return std::nullopt;

    auto edited = std::optional<Line> {};

    // The Cocoa text field's line keys, which browsers honour: cmd+backspace
    // clears to the start, cmd+delete to the end, cmd+arrows jump there.
    if (mods.command)
    {
        if (mods.control || mods.alt)
            return std::nullopt;

        switch (event.keyCode)
        {
            case KeyCode::Delete: edited = line::deleteToStart(current); break;
            case KeyCode::ForwardDelete: edited = line::deleteToEnd(current); break;
            case KeyCode::LeftArrow: edited = line::start(current); break;
            case KeyCode::RightArrow: edited = line::end(current); break;
            default: break;
        }
    }
    else if (mods.control && !mods.alt)
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
            case KeyCode::LeftArrow: edited = line::wordLeft(current); break;
            case KeyCode::RightArrow: edited = line::wordRight(current); break;
            default: break;
        }
    }

    return edited;
}

// What a key press types, if anything: no control characters, and none of
// the private-use code points macOS gives arrows and function keys.
std::optional<std::string> typedText(const Graphics::KeyEvent& event)
{
    auto& mods = event.modifiers;

    if (mods.command || mods.control || event.characters.empty())
        return std::nullopt;

    for (auto c: event.characters)
        if ((unsigned char) c < 0x20 || c == 0x7f)
            return std::nullopt;

    auto& text = event.characters;

    if (text.size() >= 3 && (unsigned char) text[0] == 0xEF
        && (unsigned char) text[1] >= 0x9C && (unsigned char) text[1] <= 0xA3)
        return std::nullopt;

    return text;
}
} // namespace

bool PanelView::editQuery(const Graphics::KeyEvent& event)
{
    auto& query = root.switcher.query;
    auto current = Line {query.getText(), query.getCaretPosition()};
    auto edited = emacsEdit(current, event);

    if (!edited)
        return false;

    // An edit re-filters and re-previews exactly as typing does.
    if (edited->text != current.text)
        query.setText(edited->text, true);

    query.setCaretPosition(edited->cursor);
    return true;
}

void PanelView::stopRenaming()
{
    renaming.reset();
    renameAllSelected = false;
    root.switcher.list.setEditing(std::nullopt);
}

void PanelView::editRename(const Graphics::KeyEvent& event)
{
    using namespace Graphics;

    auto key = event.keyCode;
    auto plain = !event.modifiers.command && !event.modifiers.control
                 && !event.modifiers.alt;

    if (key == KeyCode::Escape)
    {
        stopRenaming();
        return;
    }

    if (key == KeyCode::Return)
    {
        auto name = renaming->text;
        stopRenaming();
        onRename(name);
        return;
    }

    auto edited = emacsEdit(*renaming, event);

    if (!edited && plain)
    {
        if (key == KeyCode::Delete)
            edited = line::backspace(*renaming);
        else if (key == KeyCode::ForwardDelete)
            edited = line::deleteForward(*renaming);
        else if (key == KeyCode::LeftArrow)
            edited = line::left(*renaming);
        else if (key == KeyCode::RightArrow)
            edited = line::right(*renaming);
    }

    auto typed = std::optional<std::string> {};

    if (!edited && !event.modifiers.command && !event.modifiers.control)
        if ((typed = typedText(event)))
            edited = line::insert(*renaming, *typed);

    if (!edited)
        return;

    // The whole title is selected: what's typed replaces it, a deletion
    // clears it, a cursor move just drops the selection.
    if (renameAllSelected)
    {
        if (typed)
            edited = Line {*typed, (int) typed->size()};
        else if (edited->text.size() < renaming->text.size())
            edited = Line {};

        renameAllSelected = false;
    }

    renaming = *edited;
    root.switcher.list.setEditing(renaming, renameAllSelected);
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

    auto favourite = parseChord(settingsModel.favourite);
    auto rename = parseChord(settingsModel.rename);

    if (renaming)
        editRename(event);
    else if (rename
             && matchesChord(*rename, key, mods.control, mods.alt, mods.shift, mods.command))
    {
        if (auto title = onRenameStart())
        {
            renaming = Line {*title, (int) title->size()};
            renameAllSelected = !title->empty();
            root.switcher.list.setEditing(renaming, renameAllSelected);
        }
    }
    else if (quit && matchesChord(*quit, key, mods.control, mods.alt, mods.shift, mods.command))
        onQuitSelected();
    else if (favourite
             && matchesChord(*favourite, key, mods.control, mods.alt, mods.shift, mods.command))
        onToggleFavourite();
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
