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

Write-Host "Package verification passed: $($actual -join ', ')"
