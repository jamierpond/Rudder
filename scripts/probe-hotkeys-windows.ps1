# Tries to register each chord in chords.txt next to this script, one per line
# ("control+command+y"; command is the Windows key), as a global hotkey,
# releases it at once, and writes "<chord> free" or "<chord> taken" to
# result.txt beside it: what GlobalHotKey::isAvailable asks. Must run in the
# logged-in user's desktop session, where the shell's hotkeys are
# (scripts/probe-hotkeys-windows.sh arranges it).
$Chords = Get-Content (Join-Path $PSScriptRoot "chords.txt")

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HotKeyProbe {
    [DllImport("user32.dll")] public static extern bool RegisterHotKey(IntPtr window, int id, uint modifiers, uint key);
    [DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr window, int id);
}
"@

$modifierBits = @{ alt = 0x1; option = 0x1; control = 0x2; ctrl = 0x2; shift = 0x4; command = 0x8; win = 0x8 }
$noRepeat = 0x4000
$probeId = 0xBFFF

$results = foreach ($chord in $Chords) {
    $parts = $chord.ToLower().Split('+')
    $modifiers = $noRepeat

    foreach ($part in $parts[0..($parts.Length - 2)]) {
        $modifiers = $modifiers -bor $modifierBits[$part]
    }

    $virtualKey = [uint32][char]$parts[-1].ToUpper()
    $free = [HotKeyProbe]::RegisterHotKey([IntPtr]::Zero, $probeId, $modifiers, $virtualKey)

    if ($free) {
        [void][HotKeyProbe]::UnregisterHotKey([IntPtr]::Zero, $probeId)
    }

    $verdict = if ($free) { "free" } else { "taken" }
    "$chord $verdict"
}

$results | Set-Content (Join-Path $PSScriptRoot "result.txt")
