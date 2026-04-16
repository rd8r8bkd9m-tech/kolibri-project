<# 
 Kolibri deployment helper for Windows hosts.
 Requires Docker Desktop with WSL2 backend enabled.

 Usage:
   .\scripts\deploy_windows.ps1 -Version v1.2.0 -BackendPort 4050 -FrontendPort 8080
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [int]$BackendPort = 4050,
    [int]$FrontendPort = 8080,
    [string]$BackendImage,
    [string]$FrontendImage,
    [switch]$SkipPull
)

function Invoke-Docker {
    param(
        [string[]]$Arguments
    )
    Write-Host "[docker] $($Arguments -join ' ')"
    & docker @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Docker command failed: $($Arguments -join ' ')"
    }
}

$backendImage = if ($BackendImage) { $BackendImage } else { "ghcr.io/kolibri/kolibri-backend:$Version" }
$frontendImage = if ($FrontendImage) { $FrontendImage } else { "ghcr.io/kolibri/kolibri-frontend:$Version" }

if (-not $SkipPull) {
    Invoke-Docker -Arguments @("pull", $backendImage)
    Invoke-Docker -Arguments @("pull", $frontendImage)
}

foreach ($service in @("kolibri-backend", "kolibri-frontend")) {
    try {
        Invoke-Docker -Arguments @("stop", $service)
    } catch {
        Write-Verbose "Service $service was not running"
    }
    try {
        Invoke-Docker -Arguments @("rm", $service)
    } catch {
        Write-Verbose "Service $service had no container to remove"
    }
}

Invoke-Docker -Arguments @(
    "run", "-d",
    "--name", "kolibri-backend",
    "--restart", "unless-stopped",
    "-p", "$BackendPort:4050",
    $backendImage,
    "--listen", "4050"
)

Invoke-Docker -Arguments @(
    "run", "-d",
    "--name", "kolibri-frontend",
    "--restart", "unless-stopped",
    "-p", "$FrontendPort:80",
    $frontendImage
)

Write-Host "Kolibri backend is listening on port $BackendPort"
Write-Host "Kolibri frontend is listening on http://localhost:$FrontendPort"
