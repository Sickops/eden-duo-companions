# Add a Windows DLL to an existing .dsmod.zip.
#
#   powershell -ExecutionPolicy Bypass -File pack-windows-module.ps1 `
#       -Zip "C:\...\0100000000010000-SuperMarioOdyssey-1.0.0.dsmod.zip" `
#       -Dll "build-native-windows\0100000000010000.dll" `
#       -Out "C:\...\0100000000010000-SuperMarioOdyssey-1.0.0-windows.dsmod.zip"
#
# The module entry is added to package.json and dualscreen/manifest.json (both carry the block),
# next to the existing per-platform entries, and the DLL goes to
# dualscreen/modules/windows-x86_64/<TITLEID>.dll. The file name is the title id, which the host
# derives from the package's title_id.
param(
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Out,
    [string]$Platform = 'windows-x86_64'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not $Out) { $Out = [System.IO.Path]::ChangeExtension($Zip, $null) + "-$Platform.dsmod.zip" }
$dllItem = Get-Item -LiteralPath $Dll
$sha = (Get-FileHash -LiteralPath $dllItem.FullName -Algorithm SHA256).Hash.ToLower()

# ---- read the original entries ---------------------------------------------------------------
$src = [System.IO.Compression.ZipFile]::OpenRead($Zip)
$entries = @()
foreach ($e in $src.Entries) {
    $s = $e.Open()
    $ms = New-Object System.IO.MemoryStream
    $s.CopyTo($ms)
    $s.Close()
    $entries += [pscustomobject]@{ Name = $e.FullName; Bytes = $ms.ToArray() }
}
$src.Dispose()

$package = $entries | Where-Object { $_.Name -eq 'package.json' }
if (-not $package) { throw "package.json missing from $Zip" }
$titleId = ([System.Text.Encoding]::UTF8.GetString($package.Bytes) | ConvertFrom-Json).title_id
if (-not $titleId) { throw "package.json has no title_id" }
$entryPath = "modules/$Platform/$titleId.dll"
Write-Output "title=$titleId  platform=$Platform  sha256=$sha"

# ---- patch every JSON that carries the module block -------------------------------------------
# Insert the new library next to the existing ones: find the last "libraries" object's closing
# brace and add a sibling entry before it. Textual, so the rest of the file stays byte-identical.
$patched = 0
foreach ($e in $entries) {
    if ($e.Name -ne 'package.json' -and $e.Name -ne 'dualscreen/manifest.json') { continue }
    $text = [System.Text.Encoding]::UTF8.GetString($e.Bytes)
    $module = $text.IndexOf('"module"', [System.StringComparison]::Ordinal)
    if ($module -lt 0) { continue }
    $libs = $text.IndexOf('"libraries"', $module, [System.StringComparison]::Ordinal)
    if ($libs -lt 0) { continue }
    # The libraries object ends at the first '}' that closes it, which is the last '}' before the
    # module object's own closing brace. Walk braces from the opening one to stay exact.
    $open = $text.IndexOf('{', $libs)
    if ($open -lt 0) { continue }
    $depth = 0
    $close = -1
    for ($i = $open; $i -lt $text.Length; $i++) {
        $ch = $text[$i]
        if ($ch -eq '{') { $depth++ }
        elseif ($ch -eq '}') { $depth--; if ($depth -eq 0) { $close = $i; break } }
    }
    if ($close -lt 0) { throw "unbalanced module.libraries in $($e.Name)" }
    # Insert after the last library entry's own closing brace, not after the whitespace that
    # precedes the libraries closing brace: the result reads like the original formatting.
    $prev = $close - 1
    while ($prev -gt $open -and [char]::IsWhiteSpace($text[$prev])) { $prev-- }
    if ($text[$prev] -ne '}') { throw "unexpected module.libraries layout in $($e.Name)" }
    # Insert right after that brace, so the comma lands on the previous entry's line ("  },").
    $close = $prev + 1
    $peek = [Math]::Min(200, $text.Length - $libs)
    $pretty = $text.Substring($libs, $peek) -match '\r?\n'
    if ($pretty) {
        # Keep the file's own line ending so the patch stays a one-line change.
        $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
        $block = ',' + $nl + '   "' + $Platform + '": {' + $nl + '    "path": "' + $entryPath +
                 '",' + $nl + '    "sha256": "' + $sha + '"' + $nl + '   }'
    } else {
        $block = ',"' + $Platform + '":{"path":"' + $entryPath + '","sha256":"' + $sha + '"}'
    }
    $text = $text.Substring(0, $close) + $block + $text.Substring($close)
    # Refuse to write a package the host would reject later.
    try { $null = $text | ConvertFrom-Json } catch { throw "$($e.Name) is not valid JSON after patching: $_" }
    $e.Bytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($text)
    $patched++
    Write-Output "patched $($e.Name)"
}
if ($patched -eq 0) { throw "no module.libraries block found: this package is declarative, nothing to add" }

# ---- add the DLL -----------------------------------------------------------------------------
$entries += [pscustomobject]@{
    Name  = 'dualscreen/' + $entryPath
    Bytes = [System.IO.File]::ReadAllBytes($dllItem.FullName)
}

# ---- write the new zip -----------------------------------------------------------------------
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Force }
$zipOut = [System.IO.Compression.ZipFile]::Open($Out, [System.IO.Compression.ZipArchiveMode]::Create)
foreach ($e in $entries) {
    $ze = $zipOut.CreateEntry($e.Name, [System.IO.Compression.CompressionLevel]::Optimal)
    $st = $ze.Open()
    $st.Write($e.Bytes, 0, $e.Bytes.Length)
    $st.Close()
}
$zipOut.Dispose()

Write-Output "written $Out"
