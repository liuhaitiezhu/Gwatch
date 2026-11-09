param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDir = Join-Path $repoRoot 'build/host-max30102'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$common = @('-std=c11', '-O0', '-g', '-Wall', '-Wextra', '-Werror',
    '-I', (Join-Path $PSScriptRoot 'mocks'), '-I', (Join-Path $repoRoot 'Core/Inc'),
    '-I', (Join-Path $repoRoot 'TouchGFX/App'))
$driverExe = Join-Path $outputDir 'test_driver.exe'
& $Compiler @common (Join-Path $PSScriptRoot 'test_driver.c') `
    (Join-Path $repoRoot 'Core/Src/bsp_max30102.c') '-o' $driverExe
if ($LASTEXITCODE -ne 0) { throw 'MAX30102 driver test compilation failed' }
& $driverExe
if ($LASTEXITCODE -ne 0) { throw 'MAX30102 driver tests failed' }
$appExe = Join-Path $outputDir 'test_app.exe'
& $Compiler @common (Join-Path $PSScriptRoot 'test_app.c') '-o' $appExe
if ($LASTEXITCODE -ne 0) { throw 'MAX30102 application test compilation failed' }
& $appExe
if ($LASTEXITCODE -ne 0) { throw 'MAX30102 application tests failed' }
