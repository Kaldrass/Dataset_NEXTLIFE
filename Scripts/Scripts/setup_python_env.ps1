param(
    [string]$PythonCommand = "py",
    [string]$VenvPath = ".venv",
    [string]$TempPath = ".tmp\\python-bootstrap"
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Label,
        [scriptblock]$Action
    )

    Write-Host "==> $Label"
    & $Action

    if ($LASTEXITCODE -ne 0) {
        throw "Step failed: $Label (exit code $LASTEXITCODE)"
    }
}

function Test-PythonCommand {
    param([string]$CommandName)

    try {
        Get-Command $CommandName -ErrorAction Stop | Out-Null
        return $true
    }
    catch {
        return $false
    }
}

if (-not (Test-PythonCommand -CommandName $PythonCommand)) {
    throw "Python command '$PythonCommand' was not found. Install Python 3.11+ first, then rerun this script."
}

New-Item -ItemType Directory -Force -Path $TempPath | Out-Null
$resolvedTemp = (Resolve-Path $TempPath).Path
$env:TEMP = $resolvedTemp
$env:TMP = $resolvedTemp

Invoke-Step "Creating virtual environment at $VenvPath" {
    & $PythonCommand -m venv $VenvPath
}

$venvPython = Join-Path $VenvPath "Scripts\\python.exe"

if (-not (Test-Path $venvPython)) {
    throw "Virtual environment was created, but $venvPython was not found."
}

Invoke-Step "Upgrading pip" {
    & $venvPython -m pip install --upgrade pip
}

Invoke-Step "Installing Python dependencies" {
    & $venvPython -m pip install -r requirements.txt
}

Invoke-Step "Verifying required imports" {
    & $venvPython -c "from PIL import Image; print('Python environment OK')"
}

Write-Host ""
Write-Host "Environment ready."
Write-Host "Activate it with:"
Write-Host "  .\\$VenvPath\\Scripts\\Activate.ps1"
