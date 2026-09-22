$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $projectDir 'src\displays\tools\utf8Rus.cpp'
$source = [IO.File]::ReadAllText($sourcePath)
$entryPattern = '\{0x([0-9A-Fa-f]{4}), 0x([0-9A-Fa-f]{4}), 0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2}), ''(.)'', ''(.)''\}'
$entries = [regex]::Matches($source, $entryPattern)
if ($entries.Count -ne 30) { throw "Expected 30 UTF-8 mapping entries, found $($entries.Count)" }

$map = @{}
foreach ($entry in $entries) {
  $lowerUtf8 = [Convert]::ToUInt16($entry.Groups[1].Value, 16)
  $upperUtf8 = [Convert]::ToUInt16($entry.Groups[2].Value, 16)
  $lowerGlyph = [Convert]::ToByte($entry.Groups[3].Value, 16)
  $upperGlyph = [Convert]::ToByte($entry.Groups[4].Value, 16)
  $map[$lowerUtf8] = [PSCustomObject]@{ Lower = $lowerGlyph; Upper = $upperGlyph }
  if ($upperUtf8 -ne 0) {
    $map[$upperUtf8] = [PSCustomObject]@{ Lower = $upperGlyph; Upper = $upperGlyph }
  }
}

function Convert-Utf8Glyphs([string]$Text, [bool]$Uppercase) {
  $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
  $output = [Collections.Generic.List[byte]]::new()
  for ($i = 0; $i -lt $bytes.Count; $i++) {
    $c = $bytes[$i]
    if (($c -eq 0xC3 -or $c -eq 0xC4 -or $c -eq 0xC5) -and $i + 1 -lt $bytes.Count) {
      $key = [uint16]((([int]$c) -shl 8) -bor $bytes[$i + 1])
      if ($map.ContainsKey($key)) {
        $output.Add($(if ($Uppercase) { $map[$key].Upper } else { $map[$key].Lower }))
        $i++
        continue
      }
    }
    if ($c -eq 0xD0 -and $i + 1 -lt $bytes.Count) {
      $n = $bytes[++$i]
      if ($n -eq 0x81) { $output.Add($(if ($Uppercase) { 0xA8 } else { 0xB8 })); continue }
      if ($n -ge 144 -and $n -le 191) {
        $glyph = $n + 48
        if ($n -ge 176 -and $Uppercase) { $glyph -= 32 }
        $output.Add([byte]$glyph)
        continue
      }
    }
    if ($c -eq 0xD1 -and $i + 1 -lt $bytes.Count) {
      $n = $bytes[++$i]
      if ($n -eq 0x91) { $output.Add($(if ($Uppercase) { 0xA8 } else { 0xB8 })); continue }
      if ($n -ge 128 -and $n -le 143) {
        $glyph = $n + 112
        if ($Uppercase) { $glyph -= 32 }
        $output.Add([byte]$glyph)
        continue
      }
    }
    if ($Uppercase -and $c -ge 0x61 -and $c -le 0x7A) { $c -= 32 }
    $output.Add([byte]$c)
  }
  return ,$output.ToArray()
}

function Assert-Bytes([string]$Name, [byte[]]$Actual, [byte[]]$Expected) {
  if ($Actual.Count -ne $Expected.Count) { throw "$Name length: $($Actual.Count), expected $($Expected.Count)" }
  for ($i = 0; $i -lt $Expected.Count; $i++) {
    if ($Actual[$i] -ne $Expected[$i]) {
      throw ("{0} byte {1}: 0x{2:X2}, expected 0x{3:X2}" -f $Name, $i, $Actual[$i], $Expected[$i])
    }
  }
}

$polishLower = 'ąćęłńóśźż'
$polishUpper = 'ĄĆĘŁŃÓŚŹŻ'
$lowerGlyphs = [byte[]](0x80,0x82,0x84,0x86,0x88,0x8A,0x8C,0x8E,0x90)
$upperGlyphs = [byte[]](0x81,0x83,0x85,0x87,0x89,0x8B,0x8D,0x8F,0x91)
Assert-Bytes 'Polish lowercase' (Convert-Utf8Glyphs $polishLower $false) $lowerGlyphs
Assert-Bytes 'Polish lowercase uppercase=true' (Convert-Utf8Glyphs $polishLower $true) $upperGlyphs
Assert-Bytes 'Polish uppercase' (Convert-Utf8Glyphs $polishUpper $false) $upperGlyphs
Assert-Bytes 'Polish uppercase uppercase=true' (Convert-Utf8Glyphs $polishUpper $true) $upperGlyphs

$phrases = @(
  'Zażółć gęślą jaźń', 'Łódź', 'Środa', 'Październik', 'Głośność',
  'Połączono', 'Źródło', 'Żółty', 'Włączenie', 'Wyłączenie'
)
$regressions = @('Beyoncé', 'München', 'Český', 'Žilina', 'Måneskin', 'Радио')
foreach ($sample in $phrases + $regressions) {
  $glyphs = Convert-Utf8Glyphs $sample $false
  $charCount = [Globalization.StringInfo]::ParseCombiningCharacters($sample).Count
  if ($glyphs.Count -ne $charCount) { throw "$sample produced $($glyphs.Count) glyphs for $charCount characters" }
  $upperFromFlag = Convert-Utf8Glyphs $sample $true
  $upperExplicit = Convert-Utf8Glyphs $sample.ToUpperInvariant() $false
  Assert-Bytes "$sample uppercase" $upperFromFlag $upperExplicit
}

[PSCustomObject]@{
  MappingEntries = $entries.Count
  PolishLower = ($lowerGlyphs | ForEach-Object { '0x{0:X2}' -f $_ }) -join ' '
  PolishUpper = ($upperGlyphs | ForEach-Object { '0x{0:X2}' -f $_ }) -join ' '
  PhraseTests = $phrases.Count
  RegressionTests = $regressions.Count
  Result = 'PASS'
}