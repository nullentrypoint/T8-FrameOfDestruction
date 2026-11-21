#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Dinput8Path,
    [string]$ActionableFramesPath,
    [string]$OutputDirectory,
    [string]$GameDirectory,
    [string]$ConfigPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (!$ConfigPath) { $ConfigPath = Join-Path $PSScriptRoot 'build-release.psd1' }
$ConfigPath = (Resolve-Path -LiteralPath $ConfigPath).Path
$settings = Import-PowerShellDataFile -LiteralPath $ConfigPath
$configRoot = Split-Path -Parent $ConfigPath
$requiredKeys = @('OutputDirectory', 'MemoryAddressPath', 'ActionableFramesPath', 'ReadmePath', 'Dinput8Path', 'LoaderUrl', 'LoaderSha256')
foreach ($key in $requiredKeys) {
    if (!$settings.ContainsKey($key) -or $settings[$key] -isnot [string] -or
        ($key -ne 'Dinput8Path' -and [string]::IsNullOrWhiteSpace($settings[$key]))) {
        throw "Missing or invalid build setting: $key"
    }
}
foreach ($key in $settings.Keys) {
    if ($key -notin ($requiredKeys + 'GameDirectory')) { throw "Unknown build setting: $key" }
}
if ($settings.ContainsKey('GameDirectory') -and $settings.GameDirectory -isnot [string]) {
    throw 'Invalid build setting: GameDirectory'
}
if ($settings.LoaderSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'LoaderSha256 must contain 64 hexadecimal characters.'
}
function Resolve-ConfigPath([string]$Path) {
    if (!$Path) { return '' }
    if ([IO.Path]::IsPathRooted($Path)) { return $Path }
    return Join-Path $configRoot $Path
}
if (!$PSBoundParameters.ContainsKey('Dinput8Path')) { $Dinput8Path = Resolve-ConfigPath $settings.Dinput8Path }
if (!$PSBoundParameters.ContainsKey('ActionableFramesPath')) { $ActionableFramesPath = Resolve-ConfigPath $settings.ActionableFramesPath }
if (!$PSBoundParameters.ContainsKey('OutputDirectory')) { $OutputDirectory = Resolve-ConfigPath $settings.OutputDirectory }
$memoryAddressPath = Resolve-ConfigPath $settings.MemoryAddressPath
$readmePath = Resolve-ConfigPath $settings.ReadmePath
if (!$PSBoundParameters.ContainsKey('GameDirectory') -and $settings.ContainsKey('GameDirectory')) {
    $GameDirectory = Resolve-ConfigPath $settings.GameDirectory
}
$installDirectory = $null
if (![string]::IsNullOrWhiteSpace($GameDirectory)) {
    $installDirectory = [IO.Path]::GetFullPath($GameDirectory)
    if (!(Test-Path -LiteralPath "$installDirectory\Polaris-Win64-Shipping.exe" -PathType Leaf)) {
        $installDirectory = Join-Path $installDirectory 'Polaris\Binaries\Win64'
    }
    if (!(Test-Path -LiteralPath "$installDirectory\Polaris-Win64-Shipping.exe" -PathType Leaf)) {
        throw "Game executable was not found in GameDirectory: $GameDirectory"
    }
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio / Build Tools with MSVC v143 and the Windows 10 SDK.'
}
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ build tools were not found.' }
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'

$output = [IO.Path]::GetFullPath($OutputDirectory)
$build = Join-Path $output 'build'
$work = Join-Path $output ([Guid]::NewGuid().ToString('N'))
$stage = Join-Path $work 'package'
$config = Join-Path $stage 'plugins\T8FrameOfDestruction\config'
$archive = Join-Path $output ("T8-FrameOfDestruction-{0}-build.zip" -f (Get-Date -Format 'yyyyMMddHHmm'))
New-Item -ItemType Directory -Path $build, $config -Force | Out-Null

try {
    Copy-Item -LiteralPath $memoryAddressPath -Destination "$config\memory_address.ini"
    Copy-Item -LiteralPath $ActionableFramesPath -Destination "$config\actionable_frames.json"
    # Read explicitly as UTF-8 so Russian text also works in Windows PowerShell 5.1.
    $readme = Get-Content -LiteralPath $readmePath -Raw -Encoding UTF8
    Set-Content -LiteralPath "$stage\readme.txt" -Value $readme -Encoding UTF8

    if (!$Dinput8Path) {
        $loaderZip = Join-Path $work 'loader.zip'
        $url = $settings.LoaderUrl
        Write-Host "Downloading ASI loader: $url"
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $url -OutFile $loaderZip -UseBasicParsing
        $expectedHash = $settings.LoaderSha256
        if ((Get-FileHash -LiteralPath $loaderZip -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'ASI loader archive SHA256 mismatch.'
        }
        Expand-Archive -LiteralPath $loaderZip -DestinationPath "$work\loader"
        $Dinput8Path = "$work\loader\dinput8.dll"
    }
    Copy-Item -LiteralPath $Dinput8Path -Destination "$stage\dinput8.dll"

    # Rebuild in an isolated output directory to avoid packaging stale DLL/PDB files.
    $project = "$PSScriptRoot\dll\dll.vcxproj"
    $command = "call `"$devcmd`" -arch=x64 -host_arch=x64 && `"$msbuild`" `"$project`" /t:Rebuild /m /p:Configuration=Release /p:Platform=x64 /p:TargetName=T8FrameOfDestruction /p:OutDir=`"$build/`" /p:IntDir=`"$build/obj/`""
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "DLL build failed (exit code $LASTEXITCODE)." }

    Copy-Item -LiteralPath "$build\T8FrameOfDestruction.dll" -Destination "$stage\plugins\T8FrameOfDestruction.asi"
    Copy-Item -LiteralPath "$build\T8FrameOfDestruction.pdb" -Destination "$stage\plugins\T8FrameOfDestruction.pdb"
    $launchOptions = Join-Path $config 'launch_options.ini'
    New-Item -ItemType File -Path $launchOptions | Out-Null
    Compress-Archive -Path "$stage\*" -DestinationPath "$work\release.zip" -CompressionLevel Optimal
    Move-Item -LiteralPath "$work\release.zip" -Destination $archive
    Write-Host "Created: $archive"
    $restrictedArchive = $archive -replace '-build\.zip$', '-online-restricted-build.zip'
    Copy-Item -LiteralPath "$PSScriptRoot\config\launch_options.ini" -Destination $launchOptions -Force
    Compress-Archive -Path "$stage\*" -DestinationPath "$work\restricted.zip" -CompressionLevel Optimal
    Move-Item -LiteralPath "$work\restricted.zip" -Destination $restrictedArchive
    Write-Host "Created: $restrictedArchive"
    if ($installDirectory) {
        Clear-Content -LiteralPath $launchOptions
        Write-Host "Installing (overwriting packaged files): $installDirectory"
        Copy-Item -Path "$stage\*" -Destination $installDirectory -Recurse -Force
        Write-Host "Installed: $installDirectory"
    }
}
finally {
    Remove-Item -LiteralPath $work -Recurse -Force
}
