$ErrorActionPreference = 'Stop'

$IdfPath = $env:IDF_PATH
if ([string]::IsNullOrWhiteSpace($IdfPath)) {
    $IdfPath = Join-Path $PSScriptRoot '..\esp-idf\v5.5.2\esp-idf'
}
$IdfPath = (Resolve-Path $IdfPath).Path

$ExpectedPathFragment = Join-Path $IdfPath 'components'
$CurrentPath = $env:PATH

$needsExport = $true
if ($CurrentPath -and $CurrentPath -like "*${ExpectedPathFragment}*") {
    $needsExport = $false
}

if ($needsExport) {
    $exportScript = Join-Path $IdfPath 'export.ps1'
    if (-not (Test-Path $exportScript)) {
        Write-Error "ESP-IDF export script not found at $exportScript"
    }
    Write-Host "ESP-IDF environment not detected. Running export.ps1..."
    & $exportScript
} else {
    Write-Host "ESP-IDF environment already initialised. Skipping export.ps1."
}
