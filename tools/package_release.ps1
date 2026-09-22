param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^v\d+\.\d+\.\d+$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repoRoot 'bin\FlyArena-v0.6.7.exe'
$license = Join-Path $repoRoot 'LICENSE'
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'Build bin\FlyArena-v0.6.7.exe before packaging.'
}
if (-not (Test-Path -LiteralPath $license)) {
    throw 'FASL-1.0 LICENSE is missing.'
}
if (-not (Select-String -LiteralPath $license -SimpleMatch 'FASL-1.0' -Quiet)) {
    throw 'LICENSE does not contain the expected FASL-1.0 marker.'
}

$dist = Join-Path $repoRoot 'dist'
$packageName = "FlyArena-$Version-Windows-x64"
$stage = Join-Path $dist $packageName
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
New-Item -ItemType Directory -Path (Join-Path $stage 'data\flies') -Force | Out-Null

Copy-Item -LiteralPath $executable -Destination (Join-Path $stage 'FlyArena.exe')
Copy-Item -LiteralPath $license -Destination (Join-Path $stage 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $stage 'THIRD_PARTY_NOTICES.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'DATA_LICENSES.md') -Destination (Join-Path $stage 'DATA_LICENSES.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination (Join-Path $stage 'README.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\GETTING_STARTED_KO.md') -Destination (Join-Path $stage 'GETTING_STARTED_KO.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\GITHUB_RELEASE_KO.md') -Destination (Join-Path $stage 'GITHUB_RELEASE_KO.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE_GUIDE_KO.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repoRoot 'PERMISSION_REQUESTS.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repoRoot 'data\README.md') -Destination (Join-Path $stage 'data\README_DATA.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'data\flies\Ruby.flypack') -Destination (Join-Path $stage 'data\flies')
Copy-Item -LiteralPath (Join-Path $repoRoot 'data\flies\Azure.flypack') -Destination (Join-Path $stage 'data\flies')
Copy-Item -LiteralPath (Join-Path $repoRoot 'prepare_banc_latest.bat') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repoRoot 'prepare_v061_io_map.bat') -Destination $stage
$releaseTools = Join-Path $stage 'tools'
New-Item -ItemType Directory -Path $releaseTools -Force | Out-Null
$releaseToolNames = @(
    'build_banc_io_map.py',
    'convert_banc_latest.py',
    'prepare_banc_latest.ps1',
    'prepare_v061_io_map.ps1'
)
foreach ($toolName in $releaseToolNames) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "tools\$toolName") -Destination $releaseTools
}

$archive = Join-Path $dist "$packageName.zip"
if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive -Force
}
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Value "$hash  $packageName.zip" -Encoding ascii
Write-Host "Created $archive"
