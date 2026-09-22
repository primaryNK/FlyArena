$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Raw = Join-Path $Root "data\banc_v888\raw"
$Cache = Join-Path $Root "data\cache"
$Venv = Join-Path $Root "tools\.venv"

New-Item -ItemType Directory -Force -Path $Raw | Out-Null
New-Item -ItemType Directory -Force -Path $Cache | Out-Null

$Base = "https://storage.googleapis.com/lee-lab_brain-and-nerve-cord-fly-connectome/compiled_data/banc_888"

$Files = @(
    @{ Name = "banc_888_metrics.feather"; Url = "$Base/banc_888_metrics.feather" },
    @{ Name = "banc_888_meta.feather"; Url = "$Base/banc_888_meta.feather" },
    @{ Name = "banc_888_edgelist_simple_v3.feather"; Url = "$Base/banc_888_edgelist_simple_v3.feather" },
    @{ Name = "banc_888_neurotransmitter_prediction_v2.csv"; Url = "$Base/banc_888_neurotransmitter_prediction_v2.csv" }
)

Write-Host "============================================================"
Write-Host " FlyArena - BANC v888 one-time data setup"
Write-Host "============================================================"
Write-Host "Anatomy      : BANC materialization v888"
Write-Host "Connectivity : synapses v3 (latest simple edgelist)"
Write-Host "NT           : v2-derived neuron predictions"
Write-Host ""

$SourceManifest = @{
    downloaded_utc = (Get-Date).ToUniversalTime().ToString("o")
    source = "public mutable GCS mirror"
    base_url = $Base
    files = @()
}

foreach ($f in $Files) {
    $Dest = Join-Path $Raw $f.Name
    $Tmp = "$Dest.part"

    if (Test-Path $Dest) {
        $Bytes = (Get-Item $Dest).Length
        if ($Bytes -gt 1024) {
            Write-Host ("[FOUND] {0} ({1:N0} bytes)" -f $f.Name, $Bytes)
            $SourceManifest.files += @{
                name = $f.Name
                url = $f.Url
                bytes = $Bytes
                reused_existing = $true
            }
            continue
        }
        Remove-Item -Force $Dest
    }

    Write-Host "[FETCH] $($f.Name)"
    if (Test-Path $Tmp) { Remove-Item -Force $Tmp }

    & curl.exe --fail --location --retry 3 --retry-delay 2 --output $Tmp $f.Url
    if ($LASTEXITCODE -ne 0) {
        if (Test-Path $Tmp) { Remove-Item -Force $Tmp }
        throw "Download failed: $($f.Name)"
    }

    $Bytes = (Get-Item $Tmp).Length
    if ($Bytes -le 1024) {
        Remove-Item -Force $Tmp
        throw "Downloaded file is suspiciously small: $($f.Name)"
    }

    Move-Item -Force $Tmp $Dest
    Write-Host ("        {0:N0} bytes" -f $Bytes)

    $SourceManifest.files += @{
        name = $f.Name
        url = $f.Url
        bytes = $Bytes
        reused_existing = $false
    }
}

$SourceManifestPath = Join-Path $Raw "latest_source_manifest.json"
$SourceManifestJson = $SourceManifest | ConvertTo-Json -Depth 6
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($SourceManifestPath, $SourceManifestJson, $Utf8NoBom)

$Py = $null
if (Get-Command py.exe -ErrorAction SilentlyContinue) {
    $Py = "py.exe"
} elseif (Get-Command python.exe -ErrorAction SilentlyContinue) {
    $Py = "python.exe"
} else {
    throw "Python 3 not found. It is used only for one-time Feather conversion."
}

$VenvPython = Join-Path $Venv "Scripts\python.exe"
if (-not (Test-Path $VenvPython)) {
    Write-Host "[SETUP] Creating FlyArena-local conversion environment..."
    if ($Py -eq "py.exe") {
        & $Py -3 -m venv $Venv
    } else {
        & $Py -m venv $Venv
    }
    if ($LASTEXITCODE -ne 0) { throw "venv creation failed." }
}

Write-Host "[SETUP] Ensuring numpy/pyarrow..."
& $VenvPython -m pip install --disable-pip-version-check --upgrade pip
if ($LASTEXITCODE -ne 0) { throw "pip upgrade failed." }
& $VenvPython -m pip install --disable-pip-version-check "numpy>=2.0" "pyarrow>=18"
if ($LASTEXITCODE -ne 0) { throw "numpy/pyarrow install failed." }

Write-Host "[CONVERT] Building structurally validated latest-BANC cache..."
& $VenvPython (Join-Path $Root "tools\convert_banc_latest.py") `
    --metrics (Join-Path $Raw "banc_888_metrics.feather") `
    --meta (Join-Path $Raw "banc_888_meta.feather") `
    --edges (Join-Path $Raw "banc_888_edgelist_simple_v3.feather") `
    --nt (Join-Path $Raw "banc_888_neurotransmitter_prediction_v2.csv") `
    --out (Join-Path $Cache "banc_latest_v888_v3.farena") `
    --manifest (Join-Path $Cache "banc_latest_v888_v3_manifest.json") `
    --source-manifest $SourceManifestPath

if ($LASTEXITCODE -ne 0) {
    throw "Latest-BANC conversion failed structural validation."
}

Write-Host ""
Write-Host "SUCCESS"
Write-Host "  data\cache\banc_latest_v888_v3.farena"
Write-Host "  data\cache\banc_latest_v888_v3_manifest.json"
Write-Host ""
Write-Host "Next: prepare_v061_io_map.bat (or continue SETUP_DATA_AND_RUN.bat)"
