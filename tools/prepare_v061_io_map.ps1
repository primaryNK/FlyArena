$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Topology = Join-Path $Root "data\cache\banc_latest_v888_v3.farena"
$RawDir = Join-Path $Root "data\banc_v888\raw"
$Meta = Join-Path $RawDir "banc_888_meta.feather"
$Venv = Join-Path $Root "tools\.venv"
$Out = Join-Path $Root "data\io\banc_v888_io_v061.fio"
$Manifest = Join-Path $Root "data\io\banc_v888_io_v061_manifest.json"

if (-not (Test-Path $Topology)) {
    throw "Missing topology cache: data\cache\banc_latest_v888_v3.farena"
}

New-Item -ItemType Directory -Force -Path $RawDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null

if (-not (Test-Path $Meta)) {
    Write-Host "[FETCH] BANC metadata was not preserved; downloading only banc_888_meta.feather..."
    $Url = "https://storage.googleapis.com/lee-lab_brain-and-nerve-cord-fly-connectome/compiled_data/banc_888/banc_888_meta.feather"
    $Tmp = "$Meta.part"
    & curl.exe --fail --location --retry 3 --retry-delay 2 --output $Tmp $Url
    if ($LASTEXITCODE -ne 0) {
        if (Test-Path $Tmp) { Remove-Item -Force $Tmp }
        throw "Could not download BANC metadata."
    }
    Move-Item -Force $Tmp $Meta
} else {
    Write-Host "[FOUND] $Meta"
}

$Py = $null
if (Get-Command py.exe -ErrorAction SilentlyContinue) {
    $Py = "py.exe"
} elseif (Get-Command python.exe -ErrorAction SilentlyContinue) {
    $Py = "python.exe"
} else {
    throw "Python 3 not found."
}

$VenvPython = Join-Path $Venv "Scripts\python.exe"
if (-not (Test-Path $VenvPython)) {
    Write-Host "[SETUP] Creating project-local Python environment..."
    if ($Py -eq "py.exe") {
        & $Py -3 -m venv $Venv
    } else {
        & $Py -m venv $Venv
    }
    if ($LASTEXITCODE -ne 0) { throw "venv creation failed." }
}

& $VenvPython -m pip install --disable-pip-version-check "numpy>=2.0" "pyarrow>=18"
if ($LASTEXITCODE -ne 0) { throw "numpy/pyarrow install failed." }

Write-Host "[BUILD] Creating metadata-derived sensory/readout map..."
& $VenvPython (Join-Path $Root "tools\build_banc_io_map.py") `
  --topology $Topology `
  --meta $Meta `
  --out $Out `
  --manifest $Manifest

if ($LASTEXITCODE -ne 0) {
    throw "IO map construction failed."
}

Write-Host ""
Write-Host "V0.6.1 IO map ready."
