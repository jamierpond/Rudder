#pragma once

#include <eacp/Graphics/Graphics.h>

#include <functional>

// Summons a panel over whatever the user is doing without dragging the app's big
// window along. Four macOS behaviours fight that: a background app's window is
// never composited on a full-screen Space, activation raises the app's KEY and
// MAIN windows, keystrokes go to the ACTIVE app, and ordering a key window out
// promotes the next one. On Windows the fight is the foreground lock, and a
// hidden foreground window handing the keyboard to whatever is next in z-order.
// Call in the order declared below.
namespace rudder::quick_window
{
// BEFORE the panel takes key: that is the app a dismiss hands focus back to.
void rememberFrontmostApp();

// Rudder addition: overrides the remembered app with the one the user picked,
// so the dismiss after an Enter hands focus to the selection rather than back
// to where the panel was summoned over.
void rememberAppForHandoff(int pid);

// eacp's alwaysOnTop maps to NSFloatingWindowLevel, which macOS won't reliably
// draw on a full-screen Space.
void raiseAboveFullScreen(eacp::Graphics::Window& window);

// Takes both KEY and MAIN, since activation would otherwise bring the app's big
// window forward with the panel.
void takeKeyForTyping(eacp::Graphics::Window& window);

// Call with the panel already front-and-key. Requires installReopenHandler — the
// escalation this falls back to arrives as a reopen.
void activateForPanel();

void hideYieldingActivation(eacp::Graphics::Window& window,
                            std::function<void()> hide);

// Opening Rudder again while it runs arrives as onReopen. On macOS this also
// swallows the reopen activateForPanel's escalation comes back as, which would
// otherwise open the big window over the panel; on Windows a second Rudder.exe
// hands the reopen to the running one and exits.
void installReopenHandler(std::function<void()> onReopen);

// Off by default — a panel meant to stay put should not become draggable.
void enableWindowDragThroughHost();

void setLogger(std::function<void(const char*)> log);
} // namespace rudder::quick_window
