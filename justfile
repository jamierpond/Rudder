# Rudder dev commands. Each recipe hands off to scripts/<recipe>.mjs, which
# picks the macOS or Windows variant (scripts/<recipe>-<macos|windows>.mjs).

set windows-shell := ["cmd.exe", "/c"]

# Ninja drives cl directly on Windows, so it needs the Visual Studio environment.
msvc := if os() == "windows" { "powershell -NoProfile -ExecutionPolicy Bypass -File scripts/with-msvc.ps1" } else { "" }

# Show the recipe list by default.
default:
    @just --list

[doc('Configure (if needed) + build Rudder for the current platform')]
build:
    {{msvc}} node scripts/build.mjs

[doc('Build + run the unit tests in build-test/ (leaves the app in build/ alone)')]
test:
    {{msvc}} node scripts/test.mjs

[doc('Build + launch Rudder')]
run:
    {{msvc}} node scripts/run.mjs

[doc('Build Release + install (macOS: ~/.local/libexec/rudder/Rudder.app, linked from ~/.local/bin/rudder; Windows: %LOCALAPPDATA%\Programs\Rudder) and start it')]
install:
    {{msvc}} node scripts/install.mjs

[doc('Move branch-pinned deps (eacp, emberstore, ...) to their branch heads and rewrite lockstep.lock')]
update-deps:
    {{msvc}} node scripts/update-deps.mjs

[doc('Delete the build directory')]
clean:
    node scripts/clean.mjs

[doc('Restart the current build: no compile (macOS: keeps its Accessibility grant)')]
relaunch:
    node scripts/run.mjs --relaunch

[doc('Rebuild Assets/Icon from Source.png (needs ImageMagick 7)')]
icon:
    bash scripts/make-icons.sh

[doc('Print the live frecency store: each key decayed to now, its ranking bonus and when it was last used')]
frecency-report *args:
    node scripts/frecency-report.mjs {{args}}
