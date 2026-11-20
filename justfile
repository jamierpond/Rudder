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

[doc("Ask a Windows box (git-bash over ssh, user logged in) which chords are free global hotkeys in the user's session: just probe-hotkeys-windows jamie@tamby-windows control+command+y")]
[unix]
probe-hotkeys-windows host +chords:
    scripts/probe-hotkeys-windows.sh {{host}} {{chords}}

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

# ---- Line coverage: which lines of Rudder's own code a normal day runs ----

coverage_build_dir := "build-coverage"
coverage_cmake_arguments := "-DPOND_LINE_COVERAGE=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
coverage := "python3 " + coverage_build_dir + "/PondLineCoverage/line-coverage.py --app Rudder --program Rudder --installed-in ~/.local/libexec/rudder/Rudder.app/Contents/MacOS --own-code-folder Source --own-code-folder Lib --own-code-folder Platform --own-code-folder Client --not-own-code-folder Tests"

[doc('Line coverage is the default for now: build Release with line-coverage counters in build-coverage, install it to ~/.local/libexec/rudder/Rudder.app and start it, keeping the old and new programs for coverage-report')]
[macos]
install: coverage-build
    {{coverage}} keep
    node scripts/install.mjs --build-directory {{coverage_build_dir}} {{coverage_cmake_arguments}}
    {{coverage}} keep

[doc('Build Release + install (%LOCALAPPDATA%\Programs\Rudder) and start it; no line coverage on Windows, which builds with MSVC')]
[windows]
install:
    {{msvc}} node scripts/install.mjs

[doc('Build Release without line coverage + install (macOS: ~/.local/libexec/rudder/Rudder.app, linked from ~/.local/bin/rudder; Windows: %LOCALAPPDATA%\Programs\Rudder) and start it')]
install-without-coverage:
    {{msvc}} node scripts/install.mjs

[doc('Build Rudder with line-coverage counters (clang), in build-coverage')]
[macos]
coverage-build:
    cmake -G Ninja -S . -B {{coverage_build_dir}} -DCMAKE_BUILD_TYPE=Release {{coverage_cmake_arguments}}
    cmake --build {{coverage_build_dir}} --target Rudder

[doc('Print the code of Rudder that never ran in any kept build, largest first, and write an HTML per-line view')]
[macos]
coverage-report *arguments:
    {{coverage}} report {{arguments}}

[doc('Delete the line-coverage counts and kept programs')]
[macos]
coverage-reset *arguments:
    {{coverage}} reset {{arguments}}

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
