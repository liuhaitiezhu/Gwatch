param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputDirectory = Join-Path $projectRoot 'build/sht20-host-tests'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$executable = Join-Path $outputDirectory 'test_sht20.exe'
& $Compiler '-std=c11' '-Wall' '-Wextra' '-Werror' '-O2' `
    '-I' (Join-Path $PSScriptRoot 'mocks') `
    '-I' (Join-Path $projectRoot 'Core/Inc') `
    '-I' (Join-Path $projectRoot 'TouchGFX/App') `
    (Join-Path $PSScriptRoot 'test_sht20.c') `
    (Join-Path $projectRoot 'Core/Src/bsp_sht20.c') `
    (Join-Path $projectRoot 'Core/Src/app_freertos.c') `
    '-o' $executable
if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed.' }
& $executable
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed.' }
