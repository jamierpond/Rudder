# Rudder dev commands. Each recipe hands off to scripts/<recipe>.mjs, which
# picks the macOS or Windows variant (scripts/<recipe>-<macos|windows>.mjs).

set windows-shell := ["cmd.exe", "/c"]

# Show the recipe list by default.
default:
    @just --list

[doc('Configure (if needed) + build Rudder for the current platform')]
build:
    node scripts/build.mjs

[doc('Build + run the unit tests in build-test/ (leaves the app in build/ alone)')]
test:
    node scripts/test.mjs

[doc('Build + launch Rudder')]
run:
    node scripts/run.mjs

[doc('Build Release + install (macOS: ~/.local/libexec/rudder/Rudder.app, linked from ~/.local/bin/rudder; Windows: %LOCALAPPDATA%\Programs\Rudder) and start it')]
install:
    node scripts/install.mjs

[doc('Move branch-pinned deps (eacp, emberstore, ...) to their branch heads and rewrite lockstep.lock')]
update-deps:
    node scripts/update-deps.mjs

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
