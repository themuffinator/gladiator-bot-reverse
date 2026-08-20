# Installs the built Gladiator modules into a Quake II mod directory.
#
# The destination is a single value with one source of truth: pass -Destination,
# or set GLADIATOR_INSTALL_DIR. It used to be hardcoded here while tasks.json and
# launch.json named a different drive, so a successful-looking install could copy
# the DLL somewhere the engine never reads.
[CmdletBinding()]
param(
	[string]$Destination = $env:GLADIATOR_INSTALL_DIR,
	[string]$BuildDir = ".vscode-build"
)

$ErrorActionPreference = "Stop"

if (-not $Destination) {
	throw "No destination. Pass -Destination <path> or set GLADIATOR_INSTALL_DIR " +
		"to the mod directory inside your Quake II install (e.g. E:\Games\q2Clean\gladiator)."
}

New-Item -ItemType Directory -Path $Destination -Force | Out-Null

# Both halves of the mod must come from the same build: the game module loads
# the botlib at runtime, so installing one without the other is a silent
# version mismatch.
$modules = @(
	@{ Name = "botlib"; Patterns = @("gladiator.dll") },
	@{ Name = "game";   Patterns = @("gamex86_64.dll", "gamex86.dll") }
)

$configs = @("", "RelWithDebInfo", "Release", "Debug")
$installed = 0

foreach ($module in $modules) {
	$found = $null
	foreach ($pattern in $module.Patterns) {
		foreach ($config in $configs) {
			foreach ($sub in @("", "src\game")) {
				$candidate = Join-Path $BuildDir (Join-Path $sub (Join-Path $config $pattern))
				$candidate = $candidate -replace '\\+', '\'
				if (Test-Path $candidate) { $found = (Resolve-Path $candidate).Path; break }
			}
			if ($found) { break }
		}
		if ($found) { break }
	}

	if (-not $found) {
		Write-Warning ("Could not find the {0} module under {1}; skipping." -f $module.Name, $BuildDir)
		continue
	}

	Copy-Item -Path $found -Destination (Join-Path $Destination (Split-Path $found -Leaf)) -Force
	Write-Host ("Installed {0} -> {1}" -f $found, $Destination)
	$installed++
}

if ($installed -eq 0) {
	throw "Nothing was installed. Build the 'gladiator' and 'game' targets first."
}
