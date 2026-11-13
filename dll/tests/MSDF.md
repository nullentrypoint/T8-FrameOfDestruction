# Flying text MSDF renderer

`msdf_text.cpp` renders flying text using its own DX12 pipeline. ImGui still
renders the menu. The atlas is generated once from Windows `Fonts/consolab.ttf`
(Consolas Bold), cached in CPU memory across resize, and uploaded to a linear
RGBA8 texture. There are no new runtime assets to deploy with `dll.dll`.

- Printable ASCII is supported (including all frame-advantage numbers/signs).
  Unsupported UTF-8 code points become `?`.
- Existing size, color, screen anchor and fade settings are used. `fontShadow`
  enables the existing 1.5-pixel black outline, now evaluated as an outward
  distance threshold rather than a larger duplicate of the text.
- Atlas generation uses 64-pixel font height and a 24-texel distance range.
  Requested outlines are clamped to the available range at the displayed size.
- Both Present paths initialize/render MSDF. Resources are released after the
  overlay fence completes on resize/unload. Initialization failures are logged
  to the console and retain the previous ImGui rendering fallback.

## Offscreen check

First build `dll.vcxproj` in Release/x64. From `dll/` in an x64 Visual Studio
developer **PowerShell**, run (default project intermediate directory):

```powershell
$tmp = "$env:LOCALAPPDATA\Temp\opencode"
$sources = ([xml](Get-Content 'third_party\msdfgen\msdfgen.props')).Project.ItemGroup.ClCompile.Include.Split(';')
$objects = $sources | ForEach-Object { 'dll\x64\Release\' + [IO.Path]::GetFileNameWithoutExtension($_) + '.obj' }
cl /nologo /std:c++20 /EHsc /MD /I. tests\msdf_smoke.cpp dll\x64\Release\msdf_text.obj $objects "/Fo$tmp\msdf_smoke.obj" "/Fe$tmp\msdf_smoke.exe" /link /LTCG /OPT:REF
& "$tmp\msdf_smoke.exe" "$tmp\msdf_smoke.ppm"
```

Keep assertions enabled. This uses WARP, does not open/inject into the game,
and checks atlas/pipeline initialization, actual GPU readback, additional
outline coverage, stable fill, isolated-glyph half-opacity, full transparency,
and identical rendering after shutdown/reinitialization. When the DX12 debug
layer is installed it also rejects validation errors. The optional PPM output
shows `+9 -26 0` with a 3-pixel outline over gray.

In-game verification: use the flying-text examples in the F1 menu, check
spatial centering and fade, resize/change resolution, and unload with F11.

Startup regression: on a cold launch (or with a debugger pausing the render
thread inside `BuildAtlas` for more than five seconds), verify that initialization
finishes and the menu/overlay appear. The loader must keep the installed DX12
hooks alive while waiting for Present/atlas generation. Repeat with late
injection and with compute/copy submissions preceding the first direct queue
submission; these must not prevent queue capture.
