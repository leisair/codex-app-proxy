param(
  [ValidateSet("Debug", "Release")]
  [string]$Config = "Release",
  [switch]$Clean,
  [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"

if ($Clean -and (Test-Path $build)) {
  Remove-Item -LiteralPath $build -Recurse -Force
}

cmake -S $root -B $build -G "Visual Studio 17 2022" -A x64
cmake --build $build --config $Config

if (-not $SkipTests) {
  ctest --test-dir $build -C $Config --output-on-failure
  node (Join-Path $root "tests\verify-start-here.mjs") `
    (Join-Path $root "resources\START-HERE.html") `
    (Join-Path $root "resources\default_config.json")
}

$output = Join-Path $root "out"
if (Test-Path -LiteralPath $output) {
  Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
Copy-Item -Force (Join-Path $build "$Config\CodexProxyLauncher.exe") $output
Copy-Item -Force (Join-Path $root "resources\START-HERE.html") $output
Copy-Item -Force (Join-Path $root "resources\default_config.json") (Join-Path $output "config.json")
Copy-Item -Force (Join-Path $root "README.md") $output
. (Join-Path $root "tests\verify-package.ps1") -Path $output

Write-Host "Output written to $output"
