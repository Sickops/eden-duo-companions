# Windows first, then Android arm64; all commands run on Windows.
param(
    [string]$Python = 'python',
    [string]$Ndk = 'C:\android\ndk\android-ndk-r30',
    [switch]$WindowsOnly
)
$ErrorActionPreference = 'Stop'
$repo = Join-Path $PSScriptRoot 'eden-duo-companions-super-mario-odyssey-1.0.0'
$eden = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\eden-duo'))
$title = '0100F43008C44000'
$version = '0.9.0'
function Check-Exit([string]$Step) {
    if ($LASTEXITCODE -ne 0) { throw "$Step failed (exit $LASTEXITCODE)" }
}
& $Python (Join-Path $repo 'tools\za\gen_manifest.py')
Check-Exit 'Manifest generation'
& $Python (Join-Path $repo 'tools\za\check_manifest.py')
Check-Exit 'Manifest validation'
$env:EDEN_ROOT = $eden
& (Join-Path $PSScriptRoot 'build-windows.bat') dsmod-za
Check-Exit 'Windows module build'
$modules = @('--module', "windows-x86_64=$(Join-Path $PSScriptRoot "build-native-windows\$title.dll")")
if (-not $WindowsOnly) {
    $toolchain = Join-Path $Ndk 'build\cmake\android.toolchain.cmake'
    if (-not (Test-Path -LiteralPath $toolchain)) { throw "Android NDK not found: $Ndk" }
    $androidBuild = Join-Path $PSScriptRoot 'build-native-android-za'
    & cmake -S (Join-Path $repo 'native\modules') -B $androidBuild -G Ninja `
        "-DEDEN_SOURCE_ROOT=$eden" "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static `
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DEDEN_ACNH_BUILD_DIAGNOSTICS=OFF
    Check-Exit 'Android configuration'
    & cmake --build $androidBuild --target dsmod-za --parallel 6
    Check-Exit 'Android module build'
    $so = Join-Path $androidBuild "$title.so"
    & (Join-Path $Ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe') --strip-all $so
    Check-Exit 'Android stripping'
    $modules += @('--module', "android-arm64-v8a=$so")
}
$dist = Join-Path $PSScriptRoot 'dist-za'
$packageTemp = Join-Path $PSScriptRoot 'build-package-temp'
New-Item -ItemType Directory -Force -Path $packageTemp | Out-Null
$previousTemp = $env:TEMP
$previousTmp = $env:TMP
try {
    $env:TEMP = $packageTemp
    $env:TMP = $packageTemp
    # Android's installer accepts .so libraries only. Ship one archive per platform.
    for ($moduleIndex=0; $moduleIndex -lt $modules.Count; $moduleIndex+=2) {
        $moduleArgument=$modules[$moduleIndex+1]
        $platform=$moduleArgument.Split('=',2)[0]
        $platformDist=Join-Path $dist $platform
        & $Python (Join-Path $repo 'tools\build_dualscreen_package.py') `
            --package (Join-Path $repo 'packages\PokemonLegendsZA') --output $platformDist `
            --version $version --min-runtime 19 `
            --build-id B1F12FD919EAE86A --build-id 7222E13ECF6ADB32 --module $moduleArgument
        Check-Exit "$platform package build"
        $versionedArchive=Join-Path $dist "${title}-PokemonLegendsZA-${version}-${platform}.dsmod.zip"
        Copy-Item -LiteralPath (Join-Path $platformDist "$title.dsmod.zip") -Destination $versionedArchive -Force
        if($platform -eq 'windows-x86_64') {
            Copy-Item -LiteralPath $versionedArchive -Destination (Join-Path $dist "$title.dsmod.zip") -Force
        }
        Write-Output $versionedArchive
    }
} finally {
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
}
