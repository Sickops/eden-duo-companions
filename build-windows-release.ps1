# Build the Windows (.dsmod.zip) package of every companion in this source tree.
#
#   powershell -ExecutionPolicy Bypass -File build-windows-release.ps1
#   powershell -ExecutionPolicy Bypass -File build-windows-release.ps1 -Games smo,echoes
#
# Each archive is written to dist-windows\ as <TITLEID>-<Release>-<version>-windows.dsmod.zip and
# holds the package's declarative dualscreen\ tree plus the module built by build-windows.bat
# (build-native-windows\<TITLEID>.dll) declared as the windows-x86_64 library in package.json and
# dualscreen\manifest.json. The manifest is compacted afterwards, as the installers cap it at
# 4 MiB (tools\compact_zip.py).
#
# Release names and versions mirror tools\build_release.sh (Linux/Android); the packages added
# after it are 1.0.0, the version their README states.
param(
    [string]$Dist = (Join-Path $PSScriptRoot 'dist-windows'),
    [string]$BuildDir = (Join-Path $PSScriptRoot 'build-native-windows'),
    [string[]]$Games
)
$ErrorActionPreference = 'Stop'

$repo = Join-Path $PSScriptRoot 'eden-duo-companions-super-mario-odyssey-1.0.0'
$packager = Join-Path $repo 'tools\build_dualscreen_package.py'
$compactor = Join-Path $repo 'tools\compact_zip.py'
$python = (Get-Command python -ErrorAction Stop).Source

# Package directory, release name, version, module target (empty for declarative packages).
$table = @(
    @{ Dir = 'Persona5Royal';                          Name = 'Persona5RoyalDS';                          Version = '1.1.0'; Target = 'dsmod-p5r' },
    @{ Dir = 'MetroidDread';                           Name = 'MetroidDreadDS';                           Version = '1.0.0'; Target = 'dsmod-dread' },
    @{ Dir = 'LinksAwakening';                         Name = 'LinkAwakeningDS';                          Version = '1.0.0'; Target = '' },
    @{ Dir = 'MarioKart8Deluxe';                       Name = 'MarioKart8DeluxeDS';                       Version = '1.0.0'; Target = 'dsmod-mk8d' },
    @{ Dir = 'SuperMarioWonder';                       Name = 'SuperMarioWonderDS';                       Version = '1.0.2'; Target = 'dsmod-wonder' },
    @{ Dir = 'AnimalCrossingNH';                       Name = 'AnimalCrossingNH';                         Version = '1.0.0'; Target = 'dsmod-acnh' },
    @{ Dir = 'FireEmblemThreeHouses';                  Name = 'FireEmblemThreeHousesDS';                  Version = '1.0.0'; Target = 'dsmod-fe3h' },
    @{ Dir = 'BindingOfIsaac';                         Name = 'BindingOfIsaacDS';                         Version = '1.0.2'; Target = 'dsmod-isaac' },
    @{ Dir = 'LuminescentPlatinum';                    Name = 'BrilliantDiamondLuminescentPlatinumDS';    Version = '1.0.0'; Target = 'dsmod-lp' },
    @{ Dir = 'LuminescentPlatinumPearl';               Name = 'ShiningPearlLuminescentPlatinumDS';        Version = '1.0.0'; Target = 'dsmod-lp-pearl' },
    @{ Dir = 'CrashTeamRacingNitroFueled';             Name = 'CrashTeamRacingNitroFueledDS';             Version = '1.0.0'; Target = 'dsmod-ctr' },
    @{ Dir = 'DragonQuest3HD2D';                       Name = 'DragonQuest3HD2DDS';                       Version = '1.0.0'; Target = 'dsmod-dq3' },
    @{ Dir = 'ChainedEchoes';                          Name = 'ChainedEchoesDS';                          Version = '1.0.0'; Target = 'dsmod-ce' },
    @{ Dir = 'EchoesOfWisdom';                         Name = 'EchoesOfWisdomDS';                         Version = '1.0.0'; Target = 'dsmod-echoes' },
    @{ Dir = 'DiabloIII';                              Name = 'DiabloIIIDS';                              Version = '1.0.0'; Target = 'dsmod-d3' },
    @{ Dir = 'FarmingSimulatorNintendoSwitchEdition';  Name = 'FarmingSimulatorDS';                       Version = '1.0.0'; Target = 'dsmod-farming-simulator' },
    @{ Dir = 'BreathOfTheWild';                        Name = 'BreathOfTheWildDS';                        Version = '1.0.0'; Target = 'dsmod-breath-of-the-wild' },
    @{ Dir = 'SuperMarioOdyssey';                      Name = 'SuperMarioOdyssey';                        Version = '1.0.0'; Target = 'dsmod-smo' },
    @{ Dir = 'BelmontsCurse';                          Name = 'BelmontsCurseDS';                          Version = '1.0.0'; Target = 'dsmod-belmont' },
    @{ Dir = 'PokemonLegendsZA';                       Name = 'PokemonLegendsZADS';                       Version = '0.1.0'; Target = 'dsmod-za' },
    @{ Dir = 'YoruTomosu';                             Name = 'YoruTomosuDS';                             Version = '1.0.0'; Target = 'dsmod-yoru' },
    @{ Dir = 'VA11HallA';                              Name = 'VA11HallADS';                              Version = '1.0.0'; Target = '' }
)

# The three packages whose manifest carries no module.build_ids; tools\build_release.sh passes the
# same values on its command line.
$extraBuildIds = @{
    'ChainedEchoes'              = @('17B6A110CC529FBE', '3D730AA2C1DADC4C')
    'CrashTeamRacingNitroFueled' = @('1C68951840693051', '04D1CFEB1E0B9349', '20E862BD6C39D8AE',
                                     '22DF179B89611807', '47E60871471F1CE9', '64736428D344E78E',
                                     '67813B3F85782991', '6D693314DAC7B040', '9A5CF70301FBEB93',
                                     'D614A20DDE59E4D7', 'DFCFAFF44673EF2B')
    'DragonQuest3HD2D'           = @('4F41309B39EEBE5E')
    'PokemonLegendsZA'           = @('B1F12FD919EAE86A', '7222E13ECF6ADB32')
}

function Selected([string]$dir) {
    if (-not $Games -or $Games.Count -eq 0) { return $true }
    # powershell -File passes "-Games a,b" as one string, so accept comma-separated lists too.
    foreach ($entry in $Games) {
        foreach ($game in ($entry -split ',')) {
            if ($game.Trim() -eq $dir) { return $true }
        }
    }
    return $false
}

New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("eden-windows-package-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$built = @()
$skipped = @()
try {
    foreach ($entry in $table) {
        if (-not (Selected $entry.Dir)) { continue }
        $packageDir = Join-Path $repo "packages\$($entry.Dir)"
        if (-not (Test-Path $packageDir)) {
            Write-Warning "$($entry.Dir): no packages\$($entry.Dir) in this source tree"
            continue
        }
        $manifestPath = Join-Path $packageDir 'dualscreen\manifest.json'
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $title = $manifest.title_id
        $requiresModule = [bool]$manifest.requires_module

        $arguments = @($packager, '--package', $packageDir, '--output', $stage,
                       '--version', $entry.Version)
        if ($manifest.PSObject.Properties.Name -contains 'min_runtime') {
            $arguments += @('--min-runtime', [string]$manifest.min_runtime)
        }

        if ($requiresModule -or $entry.Target -ne '') {
            $dll = Join-Path $BuildDir "$title.dll"
            if (-not (Test-Path -LiteralPath $dll)) {
                Write-Warning "$($entry.Dir): $title.dll not built (target $($entry.Target)); run build-windows.bat $($entry.Target) first - skipped"
                $skipped += $entry.Dir
                continue
            }
            $arguments += @('--module', "windows-x86_64=$dll")
            $buildIds = @()
            if ($manifest.module -and $manifest.module.build_ids) {
                $buildIds = @($manifest.module.build_ids)
            } elseif ($extraBuildIds.ContainsKey($entry.Dir)) {
                $buildIds = $extraBuildIds[$entry.Dir]
            }
            if ($buildIds.Count -eq 0) {
                Write-Warning "$($entry.Dir): no build IDs known; add them to `$extraBuildIds - skipped"
                $skipped += $entry.Dir
                continue
            }
            foreach ($id in $buildIds) { $arguments += @('--build-id', $id) }
        }

        $archive = & $python @arguments
        if ($LASTEXITCODE -ne 0 -or -not $archive) {
            Write-Warning "$($entry.Dir): packaging failed"
            $skipped += $entry.Dir
            continue
        }
        $archive = ($archive | Select-Object -Last 1).Trim()
        $final = Join-Path $Dist "$title-$($entry.Name)-$($entry.Version)-windows.dsmod.zip"
        Move-Item -LiteralPath $archive -Destination $final -Force
        & $python $compactor $final | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "$($entry.Dir): compacting failed"
            $skipped += $entry.Dir
            continue
        }
        $size = (Get-Item -LiteralPath $final).Length
        Write-Output ("OK   {0,-46} {1} bytes" -f (Split-Path $final -Leaf), $size)
        $built += $final
    }
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output ""
Write-Output ("built {0} package(s) in {1}" -f $built.Count, $Dist)
if ($skipped.Count -gt 0) {
    Write-Output ("skipped: " + ($skipped -join ', '))
}
