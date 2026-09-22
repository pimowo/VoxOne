param(
    [string]$ReferenceRoot = "reference\yoradio-main",
    [string]$OutputPath = "docs\reference_yoradio_sha256.txt"
)

$projectRoot = Split-Path -Parent $PSScriptRoot
$referencePath = Join-Path $projectRoot $ReferenceRoot
$manifestPath = Join-Path $projectRoot $OutputPath

if (-not (Test-Path -LiteralPath $referencePath -PathType Container)) {
    throw "Reference directory not found: $referencePath"
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# SHA-256 manifest for reference/yoradio-main")
$lines.Add("# yoRadio version: 0.9.720")
$lines.Add("# Format: SHA256 *relative/path")

Get-ChildItem -LiteralPath $referencePath -Recurse -File |
    Sort-Object { $_.FullName.Substring($referencePath.Length + 1).Replace('\', '/') } |
    ForEach-Object {
        $relative = $_.FullName.Substring($referencePath.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines.Add("$hash *$relative")
    }

$manifestDirectory = Split-Path -Parent $manifestPath
[System.IO.Directory]::CreateDirectory($manifestDirectory) | Out-Null
[System.IO.File]::WriteAllLines(
    $manifestPath,
    $lines,
    [System.Text.UTF8Encoding]::new($false)
)

Write-Host "Wrote $($lines.Count - 3) file hashes to $manifestPath"