[CmdletBinding()]
param(
    [switch]$Watch,
    [string]$EngineRoot
)

$ErrorActionPreference = 'Stop'

function Find-UnrealBuildScript {
    param([string]$Association, [string]$OverrideRoot)

    if ($OverrideRoot) {
        $installations = @($OverrideRoot)
    } else {
        if (-not $Association) {
            throw 'The project has no EngineAssociation. Pass -EngineRoot with your Unreal Engine installation directory.'
        }

        $installations = @(
            $registered = Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
            if ($registered) {
                $entry = $registered.PSObject.Properties[$Association]
                if ($entry) { $entry.Value }
            }

            foreach ($registryBase in @('HKLM:\SOFTWARE', 'HKLM:\SOFTWARE\WOW6432Node')) {
                $key = "$registryBase\EpicGames\Unreal Engine\$Association"
                $entry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
                if ($entry) { $entry.InstalledDirectory }
            }

            if ($env:ProgramData) {
                $manifest = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
                if (Test-Path -LiteralPath $manifest -PathType Leaf) {
                    try {
                        $launcher = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
                        $launcher.InstallationList |
                            Where-Object { $_.AppName -eq "UE_$Association" } |
                            ForEach-Object { $_.InstallLocation }
                    } catch {
                        Write-Verbose "Could not read the Epic Launcher installation list: $_"
                    }
                }
            }
        )
    }

    foreach ($installation in $installations) {
        if ([string]::IsNullOrWhiteSpace($installation)) { continue }

        $candidate = Join-Path $installation 'Engine\Build\BatchFiles\Build.bat'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).ProviderPath
        }
    }

    if ($OverrideRoot) {
        throw "Could not find Engine\Build\BatchFiles\Build.bat under '$OverrideRoot'."
    }
    throw "Could not find the Unreal Engine installation '$Association'. Pass -EngineRoot with the installation directory."
}

function Test-GradingPluginInUse {
    $binaryPrefix = (Join-Path $pluginDirectory 'Binaries\Win64').TrimEnd('\') + '\'
    foreach ($editor in (Get-Process -Name 'UE4Editor', 'UE4Editor-Cmd' -ErrorAction SilentlyContinue)) {
        try {
            $modules = $editor.Modules
        } catch {
            Write-Verbose "Could not inspect Unreal Editor process $($editor.Id): $_"
            continue
        }

        foreach ($module in $modules) {
            if ($module.FileName.StartsWith($binaryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                return $true
            }
        }
    }
    return $false
}

function Get-GradingBuildState {
    $files = @(
        Get-Item -LiteralPath $projectPath, $pluginPath
        Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File
    )
    # Track inputs only. Compiler outputs must not trigger another build.
    return ($files | Sort-Object FullName | ForEach-Object {
        "$($_.FullName)|$($_.Length)|$($_.LastWriteTimeUtc.Ticks)"
    }) -join "`n"
}

function Invoke-GradingBuild {
    Write-Host 'Checking and building GradingRuntime...'
    & $buildScript UE4Editor Win64 Development "-Project=$projectPath" "-Plugin=$pluginPath" -NoHotReload -WaitMutex | Out-Host
    return $LASTEXITCODE
}

$projectDirectory = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
$projects = @(Get-ChildItem -LiteralPath $projectDirectory -Filter '*.uproject' -File)
if ($projects.Count -ne 1) {
    throw "Expected one .uproject in '$projectDirectory'."
}

$projectPath = $projects[0].FullName
$pluginDirectory = Join-Path $projectDirectory 'Plugins\GradingRuntime'
$pluginPath = Join-Path $pluginDirectory 'GradingRuntime.uplugin'
$sourceDirectory = Join-Path $pluginDirectory 'Source'
if (-not (Test-Path -LiteralPath $pluginPath -PathType Leaf)) {
    throw "Missing plugin descriptor: '$pluginPath'."
}
if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) {
    throw "Missing plugin source: '$sourceDirectory'."
}

$project = Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json
$buildScript = Find-UnrealBuildScript -Association $project.EngineAssociation -OverrideRoot $EngineRoot
Write-Host "Project $projectPath"
Write-Host "Unreal build tool $buildScript"

if (-not $Watch) {
    if (Test-GradingPluginInUse) {
        throw 'This project has GradingRuntime loaded. Close its Unreal Editor or standalone game, or run with -Watch to queue the build.'
    }
    exit (Invoke-GradingBuild)
}

Write-Host 'Watching GradingRuntime source and project descriptors. Press Ctrl+C to stop.'
$lastObservedState = $null
$lastAttemptedState = $null
$waitingForEditor = $false

while ($true) {
    try {
        $state = Get-GradingBuildState
    } catch [System.Management.Automation.ItemNotFoundException], [System.IO.IOException] {
        # Git or the editor may briefly remove a file while replacing it.
        $lastObservedState = $null
        Start-Sleep -Seconds 2
        continue
    }
    # Wait for two matching scans so a batch of saves or a Git checkout can settle.
    if ($state -ne $lastAttemptedState -and $state -eq $lastObservedState) {
        if (Test-GradingPluginInUse) {
            if (-not $waitingForEditor) {
                Write-Host 'Build queued. Close the editor or standalone game for this project.'
                $waitingForEditor = $true
            }
        } else {
            $waitingForEditor = $false
            $buildExitCode = Invoke-GradingBuild
            if ($buildExitCode -eq 0) {
                $lastAttemptedState = $state
                Write-Host 'Build is up to date.'
            } elseif (-not (Test-GradingPluginInUse)) {
                $lastAttemptedState = $state
                Write-Warning "Build failed (exit $buildExitCode). Watching for the next change."
            }
        }
    }

    $lastObservedState = $state
    Start-Sleep -Seconds 2
}
