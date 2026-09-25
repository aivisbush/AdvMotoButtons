# Builds dist\MotoButtonsTool.exe: a single portable exe with the Moto Buttons
# Android app inside. Needs only Windows 10/11 (.NET Framework 4.8 ships with it).
# Build the Android app first (Apps\Android: gradlew assembleRelease). The release build is
# embedded so it matches the one on the setup page (Android rejects a differently signed update).
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$apk = Join-Path $here '..\Android\app\build\outputs\apk\release\app-release.apk'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$dist = Join-Path $here 'dist'

if (-not (Test-Path $apk)) { throw "Android app not built: $apk" }
New-Item -ItemType Directory -Force $dist | Out-Null

& $csc /nologo /target:winexe /optimize+ `
    /out:"$dist\MotoButtonsTool.exe" `
    /reference:System.Windows.Forms.dll /reference:System.Drawing.dll `
    /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll `
    /reference:System.Web.Extensions.dll `
    /resource:"$apk",MotoButtons.apk `
    (Join-Path $here 'MotoButtonsTool.cs')
if ($LASTEXITCODE -ne 0) { throw "Compile failed" }
Get-Item "$dist\MotoButtonsTool.exe" | Select-Object Name, Length
