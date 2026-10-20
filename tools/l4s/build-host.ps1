param([string]$Compiler='H:\tools\msys64\ucrt64\bin\gcc.exe')
$ErrorActionPreference='Stop'
$env:PATH=(Split-Path -Parent $Compiler)+';'+$env:PATH
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$hostOutput=Join-Path $repoRoot 'build/host'
New-Item -ItemType Directory -Path $hostOutput -Force | Out-Null
& $Compiler -std=c11 -Wall -Wextra -Werror -shared -O2 `
    -I (Join-Path $repoRoot 'App/app') `
    -I (Join-Path $repoRoot 'App') `
    (Join-Path $repoRoot 'App/app/l4s_protocol.c') `
    (Join-Path $repoRoot 'App/app/l4s_io.c') `
    (Join-Path $repoRoot 'App/app/l4s_settings.c') `
    (Join-Path $repoRoot 'App/app/l4s_store.c') `
    (Join-Path $repoRoot 'App/app/l4s_macros.c') `
    (Join-Path $PSScriptRoot 'macros_host.c') `
    (Join-Path $PSScriptRoot 'store_host.c') `
    (Join-Path $PSScriptRoot 'io_host.c') `
    (Join-Path $repoRoot 'App/ui/l4s.c') `
    (Join-Path $repoRoot 'App/font.c') `
    (Join-Path $repoRoot 'App/external/printf/printf.c') `
    (Join-Path $PSScriptRoot 'display_host.c') `
    (Join-Path $PSScriptRoot 'host.c') `
    -o (Join-Path $hostOutput 'l4s_host.dll')
if ($LASTEXITCODE -ne 0) { throw 'Host protocol library failed to build' }
