param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v\d+\.\d+\.\d+$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$manifest = Join-Path $repoRoot 'PUBLIC_SOURCE_MANIFEST.txt'
$dist = Join-Path $repoRoot 'dist'
$packageName = "FlyArena-$Version-Source"
$stage = Join-Path $dist $packageName
$archive = Join-Path $dist "$packageName.zip"

if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw 'PUBLIC_SOURCE_MANIFEST.txt is missing.'
}

$resolvedDist = [IO.Path]::GetFullPath($dist).TrimEnd('\') + '\'
$resolvedStage = [IO.Path]::GetFullPath($stage)
if (-not $resolvedStage.StartsWith(
        $resolvedDist,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to clean a staging path outside dist.'
}

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

$entries = Get-Content -LiteralPath $manifest |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -and -not $_.StartsWith('#') }

$seen = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $entries) {
    if ([IO.Path]::IsPathRooted($entry) -or
        $entry -split '[\\/]' -contains '..') {
        throw "Unsafe manifest path: $entry"
    }
    if (-not $seen.Add($entry)) {
        throw "Duplicate manifest path: $entry"
    }

    $source = [IO.Path]::GetFullPath((Join-Path $repoRoot $entry))
    $repoPrefix = $repoRoot.TrimEnd('\') + '\'
    if (-not $source.StartsWith(
            $repoPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Manifest path leaves the repository: $entry"
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Manifest file is missing: $entry"
    }

    $destination = Join-Path $stage $entry
    $destinationDirectory = Split-Path -Parent $destination
    if ($destinationDirectory) {
        New-Item -ItemType Directory -Path $destinationDirectory -Force |
            Out-Null
    }
    Copy-Item -LiteralPath $source -Destination $destination
}

if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive -Force
}
Compress-Archive -LiteralPath $stage -DestinationPath $archive `
    -CompressionLevel Optimal

$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumFile = Join-Path $dist 'SHA256SUMS.txt'
$checksumLine = "$hash  $packageName.zip"
if (Test-Path -LiteralPath $checksumFile) {
    $otherChecksums = Get-Content -LiteralPath $checksumFile |
        Where-Object { $_ -notmatch "  $([regex]::Escape($packageName)).zip$" }
    @($otherChecksums) + $checksumLine |
        Set-Content -LiteralPath $checksumFile -Encoding ascii
} else {
    Set-Content -LiteralPath $checksumFile -Value $checksumLine -Encoding ascii
}

Write-Host "Created $archive with $($seen.Count) approved files"
