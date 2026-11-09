#Requires -Version 5.1
[CmdletBinding()]
param(
    [int]$GameProcessId,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (!$OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot '../artifacts/steam-inspection' }
if ($GameProcessId) {
    $game = Get-Process -Id $GameProcessId
} else {
    $games = @(Get-Process -Name 'Polaris-Win64-Shipping' -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Start one game instance or specify -GameProcessId.' }
    $game = $games[0]
}
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ tools are required.' }
$dumpbins = @(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe')
if ($LASTEXITCODE -ne 0 -or !$dumpbins.Count) { throw 'dumpbin.exe was not found.' }
$dumpbin = $dumpbins | Sort-Object | Select-Object -Last 1
$modules = @($game.Modules)
if (!$modules.Count) { throw 'Cannot enumerate game modules. Use a 64-bit PowerShell with access to the game.' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null

$inventory = @($modules | ForEach-Object {
    [ordered]@{
        Name = $_.ModuleName
        Path = $_.FileName
        Base = ('0x{0:X}' -f $_.BaseAddress.ToInt64())
        Size = $_.ModuleMemorySize
    }
})
[ordered]@{
    CapturedAt = (Get-Date).ToUniversalTime().ToString('o')
    ProcessId = $game.Id
    Executable = $game.Path
    Modules = $inventory
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$output/modules.json" -Encoding UTF8

$imports = & $dumpbin /nologo /imports $game.Path
if ($LASTEXITCODE -ne 0) { throw 'Executable import inspection failed.' }
$imports | Set-Content -LiteralPath "$output/game-imports.txt" -Encoding UTF8

# These strings are static clues, not proof that an interface is used at runtime.
$bytes = [IO.File]::ReadAllBytes($game.Path)
$text = [Text.Encoding]::ASCII.GetString($bytes)
$versions = @([regex]::Matches($text, '(?:Steam[A-Za-z_]*|STEAM[A-Z_]+)[0-9]{3}\x00') |
    ForEach-Object { $_.Value.TrimEnd([char]0) } | Sort-Object -Unique)
$versions | Set-Content -LiteralPath "$output/interface-strings.txt" -Encoding UTF8
$text = $null
$bytes = $null

$index = 0
$candidates = @($modules | Where-Object { $_.ModuleName -match '^(steam_api.*|EOSSDK.*)\.dll$' })
foreach ($module in $candidates) {
    $index++
    $prefix = '{0:D2}-{1}' -f $index, $module.ModuleName
    $exports = & $dumpbin /nologo /exports $module.FileName
    if ($LASTEXITCODE -ne 0) { throw "Export inspection failed: $($module.FileName)" }
    $exports | Set-Content -LiteralPath "$output/$prefix-exports.txt" -Encoding UTF8
    [ordered]@{
        Path = $module.FileName
        Base = ('0x{0:X}' -f $module.BaseAddress.ToInt64())
        SHA256 = (Get-FileHash -LiteralPath $module.FileName -Algorithm SHA256).Hash
        FileVersion = $module.FileVersionInfo.FileVersion
    } | ConvertTo-Json | Set-Content -LiteralPath "$output/$prefix.json" -Encoding UTF8
}
Write-Host "Saved inspection to $output"
Write-Host 'Static interface strings:'
$versions
Write-Host 'Loaded SDK candidates:'
$candidates | Select-Object ModuleName, FileName | Format-Table -AutoSize
