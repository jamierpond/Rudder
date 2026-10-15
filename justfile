# Rudder dev command runner. Every recipe is a thin one-liner delegating to a
# Node script in Scripts/ — the dispatchers (compile.mjs, run.mjs) pick the
# platform variant (Scripts/<cmd>-<macos|windows>.mjs) from process.platform,
# so the same `just` commands work everywhere.

set windows-shell := ["cmd.exe", "/c"]

# Show the recipe list by default.
default:
    @just --list

[doc('Configure (if needed) + build Rudder for the current platform')]
build:
    node Scripts/compile.mjs

[doc('Build + launch Rudder')]
run:
    node Scripts/run.mjs

[doc('Restart the current build: no compile (macOS: keeps its Accessibility grant)')]
relaunch:
    node Scripts/run.mjs --relaunch

[doc('Build Release + install (macOS: /Applications/Rudder.app; Windows: %LOCALAPPDATA%\Programs\Rudder) and start it')]
install:
    node Scripts/install.mjs

[doc('Refetch branch-pinned deps (eacp, emberstore, ...) in every configured build tree')]
update-deps:
    node Scripts/update-deps.mjs

[doc('Delete the build directory')]
clean:
    node Scripts/clean.mjs

[doc('Build + run the unit tests in build-test/ (leaves the app in build/ alone)')]
test:
    node Scripts/test.mjs
