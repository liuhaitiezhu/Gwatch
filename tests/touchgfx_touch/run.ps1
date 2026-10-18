param(
    [string]$CCompiler = 'C:/mingw64/bin/gcc.exe',
    [string]$CxxCompiler = 'C:/mingw64/bin/g++.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $projectRoot 'build/touchgfx-touch-host-tests'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$driverObject = Join-Path $outputDirectory 'bsp_ft6336.o'
$executable = Join-Path $outputDirectory 'test_touch.exe'
$includes = @(
    '-I', (Join-Path $PSScriptRoot 'mocks'),
    '-I', (Join-Path $projectRoot 'Core/Inc'),
    '-I', (Join-Path $projectRoot 'TouchGFX/target'),
    '-I', (Join-Path $projectRoot 'Middlewares/ST/touchgfx/framework/include')
)
& $CCompiler '-std=c11' '-Wall' '-Wextra' '-Werror' '-O2' @includes `
    '-c' (Join-Path $projectRoot 'Core/Src/bsp_ft6336.c') '-o' $driverObject
if ($LASTEXITCODE -ne 0) { throw 'Touch driver compilation failed.' }
& $CxxCompiler '-std=c++11' '-Wall' '-Wextra' '-Werror' '-O2' @includes `
    (Join-Path $PSScriptRoot 'test_touch.cpp') `
    (Join-Path $projectRoot 'TouchGFX/target/STM32TouchController.cpp') `
    $driverObject '-o' $executable
if ($LASTEXITCODE -ne 0) { throw 'Touch adapter test compilation failed.' }
& $executable
if ($LASTEXITCODE -ne 0) { throw 'Touch adapter tests failed.' }
