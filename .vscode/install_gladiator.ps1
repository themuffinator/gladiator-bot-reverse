$ErrorActionPreference = "Stop"

$dest = "C:\q2Clean\gladiator"
New-Item -ItemType Directory -Path $dest -Force | Out-Null

$candidates = @(
	".vscode-build\gladiator.dll",
	".vscode-build\RelWithDebInfo\gladiator.dll",
	".vscode-build\Release\gladiator.dll",
	".vscode-build\Debug\gladiator.dll"
)

$src = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $src) {
	throw "Could not find gladiator.dll in build output."
}

$resolved = (Resolve-Path $src).Path
Copy-Item -Path $resolved -Destination (Join-Path $dest "gladiator.dll") -Force
Write-Host "Installed $resolved -> $dest\gladiator.dll"
