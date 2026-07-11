param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath $Path).Path
$expected = @(
  "CodexProxyLauncher.exe",
  "config.json",
  "README.md",
  "START-HERE.html"
) | Sort-Object
$actual = Get-ChildItem -LiteralPath $root -Force | ForEach-Object { $_.Name } | Sort-Object

if (Compare-Object -ReferenceObject $expected -DifferenceObject $actual) {
  throw "Package contents differ from the four documented root files. Actual: $($actual -join ', ')"
}

foreach ($name in $expected) {
  $item = Get-Item -LiteralPath (Join-Path $root $name)
  if ($item.PSIsContainer -or $item.Length -eq 0) {
    throw "Package item is empty or not a file: $name"
  }
}

$exe = Join-Path $root "CodexProxyLauncher.exe"
$iconCount = & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -Command `
  "Add-Type -AssemblyName System.Drawing; `$icon=[System.Drawing.Icon]::ExtractAssociatedIcon('$($exe.Replace("'", "''"))'); if (`$icon) { 1 } else { 0 }"
if ($iconCount -ne 1) {
  throw "CodexProxyLauncher.exe does not expose an embedded application icon"
}

Write-Host "Package verification passed: $($actual -join ', ')"
