param([ValidateSet('Release','Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$cmake = Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installation) {
            $cmake = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        }
    }
}
if (-not $cmake -or -not (Test-Path -LiteralPath $cmake)) { throw 'Install Visual Studio C++ and CMake tools first.' }
$build = Join-Path $taskRoot 'build'
& $cmake -S $taskRoot -B $build -A x64
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE) { throw 'Build failed.' }
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
& $ctest --test-dir $build -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed.' }
$dist = Join-Path $taskRoot 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item -LiteralPath (Join-Path $build "$Configuration\RainRegExp.exe") -Destination $dist
foreach ($file in @('README.md','COPYRIGHT.txt','LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot $file) -Destination $dist
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\pcre\LICENCE') -Destination (Join-Path $dist 'PCRE-LICENCE.txt')
Copy-Item -LiteralPath (Join-Path $taskRoot 'third_party\pcre\AUTHORS') -Destination (Join-Path $dist 'PCRE-AUTHORS.txt')
if (-not (Test-Path -LiteralPath (Join-Path $dist 'RainRegExp.ini'))) {
    Copy-Item -LiteralPath (Join-Path $taskRoot 'RainRegExp.ini') -Destination $dist
}
Write-Host "Ready: $(Join-Path $dist 'RainRegExp.exe')"
$versionMatch = [regex]::Match([IO.File]::ReadAllText((Join-Path $taskRoot 'CMakeLists.txt')), 'project\(RainRegExp VERSION (\d+\.\d+)\s')
if (-not $versionMatch.Success) { throw 'Expected a major.minor version in CMakeLists.txt.' }
$package = Join-Path $dist ('RainRegExp_' + $versionMatch.Groups[1].Value + '.zip')
$packageFiles = @((Join-Path $dist 'RainRegExp.exe'), (Join-Path $taskRoot 'RainRegExp.ini'))
foreach ($file in @('README.md','COPYRIGHT.txt','LICENSE.txt','PCRE-LICENCE.txt','PCRE-AUTHORS.txt')) {
    $packageFiles += Join-Path $dist $file
}
Compress-Archive -LiteralPath $packageFiles -DestinationPath $package -Force
Write-Host "Package: $package"
