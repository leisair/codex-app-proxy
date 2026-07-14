param(
  [Parameter(Mandatory = $true)]
  [string]$Path
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$iconPath = (Resolve-Path -LiteralPath $Path).Path
$bytes = [System.IO.File]::ReadAllBytes($iconPath)
$stream = [System.IO.MemoryStream]::new($bytes, $false)
$reader = [System.IO.BinaryReader]::new($stream)

try {
  if ($reader.ReadUInt16() -ne 0 -or $reader.ReadUInt16() -ne 1) {
    throw "Not a Windows ICO file: $Path"
  }

  $count = $reader.ReadUInt16()
  $expectedSizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
  if ($count -ne $expectedSizes.Count) {
    throw "Expected $($expectedSizes.Count) icon sizes, found $count"
  }

  for ($index = 0; $index -lt $count; $index++) {
    $widthByte = $reader.ReadByte()
    $heightByte = $reader.ReadByte()
    $reader.ReadByte() | Out-Null
    $reader.ReadByte() | Out-Null
    $reader.ReadUInt16() | Out-Null
    $reader.ReadUInt16() | Out-Null
    $payloadSize = $reader.ReadUInt32()
    $payloadOffset = $reader.ReadUInt32()

    $width = if ($widthByte -eq 0) { 256 } else { [int]$widthByte }
    $height = if ($heightByte -eq 0) { 256 } else { [int]$heightByte }
    if ($width -ne $expectedSizes[$index] -or $height -ne $expectedSizes[$index]) {
      throw "Unexpected icon size at index $index`: ${width}x${height}"
    }

    $frameBytes = [byte[]]::new($payloadSize)
    [Array]::Copy($bytes, $payloadOffset, $frameBytes, 0, $payloadSize)
    $frameStream = [System.IO.MemoryStream]::new($frameBytes, $false)
    $bitmap = [System.Drawing.Bitmap]::new($frameStream)
    try {
      foreach ($point in @(
        [Drawing.Point]::new(0, 0),
        [Drawing.Point]::new($bitmap.Width - 1, 0),
        [Drawing.Point]::new(0, $bitmap.Height - 1),
        [Drawing.Point]::new($bitmap.Width - 1, $bitmap.Height - 1)
      )) {
        if ($bitmap.GetPixel($point.X, $point.Y).A -ne 0) {
          throw "Icon frame ${width}x${height} has an opaque corner at ($($point.X),$($point.Y))"
        }
      }
    } finally {
      $bitmap.Dispose()
      $frameStream.Dispose()
    }
  }
} finally {
  $reader.Dispose()
  $stream.Dispose()
}

Write-Host "Icon verification passed: $($expectedSizes -join ', ') with transparent corners"
