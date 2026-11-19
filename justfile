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

[doc("Run the unit tests on a Windows box over ssh (git-bash shell) with this checkout's changes applied")]
[unix]
test-windows host="jamie@tamby-windows":
    scripts/test-windows-remote.sh {{host}}

# Builds in build-test/, as `just test` does, so the app in build/ keeps its
# Accessibility grant.
[doc("Build everything, then run clang-tidy (eacp's checks) over Rudder's sources; fails on any finding")]
[unix]
tidy:
    cmake -G Ninja -B build-test -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build build-test
    scripts/tidy.sh build-test

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

[doc('Print the live frecency store: each key usage decayed to now and when it was last used')]
frecency-report *args:
    node scripts/frecency-report.mjs {{args}}

[doc('Print how Jamie uses his apps from their session logs: feature counts, never-used features, abandoned pickers, slow picks')]
usage-report *args:
    node scripts/usage-report.mjs {{args}}

[doc('clang-format every tracked source in place')]
[unix]
format:
    git ls-files '*.h' '*.cpp' '*.mm' | xargs clang-format -i
