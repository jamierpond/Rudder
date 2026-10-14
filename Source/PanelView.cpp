#include "PanelView.h"
#include "Keybinding.h"

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

        if (auto icon = iconRef(item.appId))
            g.drawImage(icon, {row.x + 8.f, y + 6.f, 28.f, 28.f});

        auto textArea = Rect {row.x + 46.f, y, row.w - 54.f, rowHeight};

        g.setColour(text);
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
    addChildComponent(grant);
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

    for (auto* caption: {&summonCaption, &backCaption, &accessibilityCaption})
        caption->setColour(text);

    addChildren({title,
                 summonCaption,
                 summonChord,
                 backCaption,
                 backChord,
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
    error.setBounds(area.removeFromTop(24.f));
    row(accessibilityCaption, accessibility);

    done.setBounds(area.removeFromBottom(46.f).removeFromRight(90.f).inset(0.f, 9.f));
}

void SettingsPage::paint(UI::Graphics& g)
{
    g.setColour(divider);
    g.fillRect({0.f, queryHeight - 1.f, getWidth(), 1.f});
}

PanelRoot::PanelRoot()
{
    addAndMakeVisible(switcher);
    addChildComponent(settings);
}

void PanelRoot::resized()
{
    switcher.setBounds(getLocalBounds());
    settings.setBounds(getLocalBounds());
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
    settings.accessibility.onClick = [this] { onRequestAccessibility(); };
    settings.done.onClick = [this] { onCloseSettings(); };
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
    settings.error.setText(model.error);
    settings.accessibility.setText(model.accessibilityTrusted ? "Allowed" : "Allow…");

    root.switcher.footer.setText(model.summonLabel + " switch   ·   " + model.backLabel
                                 + " back");
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

void PanelView::keyDown(const Graphics::KeyEvent& event)
{
    using namespace Graphics;

    if (!capturing.empty())
    {
        recordChord(event);
        return;
    }

    auto ctrl = event.modifiers.control;
    auto key = event.keyCode;

    if (settingsOpen())
    {
        if (key == KeyCode::Escape)
            onCancel();
        else
            ComponentHost::keyDown(event);

        return;
    }

    if (key == KeyCode::DownArrow || (ctrl && key == KeyCode::N))
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
