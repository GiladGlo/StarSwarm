# Builds Star Swarm for Android: the game + raylib as libmain.so (NDK + CMake) inside a NativeActivity APK.
# No Gradle and no Java code: aapt2 packs the manifest and icons, zipalign + apksigner finish the APK.
#
#   .\build-apk.ps1                      # arm64-v8a (phones) + x86_64 (emulator) -> build-android\StarSwarm.apk
#   .\build-apk.ps1 -Abis x86_64 -Run    # emulator-only build, then install and launch on the running device
#
# Needs the Android SDK (platform, build-tools, NDK, CMake) and a JDK; Android Studio installs all of them.
# Signed with the standard debug key (~/.android/debug.keystore, created if missing): fine for testing and
# sideloading. A Play Store release needs your own upload key.
param(
    [string[]]$Abis = @("arm64-v8a", "x86_64"),
    [string]$Config = "Release",
    [switch]$Install,
    [switch]$Run
)
$ErrorActionPreference = "Stop"
$code = Split-Path $PSScriptRoot -Parent                       # ...\Code
$out  = Join-Path $code "build-android"
$pkg  = "io.github.giladglo.starswarm"
$minSdk = 24

function VersionOf($name) { $v = ($name -replace '^\D+', '') -replace '[^\d.].*$', ''; if ($v -notmatch '\.') { $v += '.0' }; [version]$v }   # "android-37.0", "36.0.0", "30.0.16248370"
function Latest($dir) { Get-ChildItem $dir -Directory | Where-Object { $_.Name -match '\d' } | Sort-Object { VersionOf $_.Name } | Select-Object -Last 1 }
function Run($exe) {   # native tools: judge by exit code (Windows PowerShell would otherwise treat warnings on stderr as errors)
    $ErrorActionPreference = "Continue"
    & $exe @args 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path $exe -Leaf) failed (exit $LASTEXITCODE)" }
}

# ---- tools ----
$sdk = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT, "$env:LOCALAPPDATA\Android\Sdk") | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $sdk) { throw "Android SDK not found (set ANDROID_HOME)" }
$ndk   = (Latest "$sdk\ndk").FullName
$bt    = (Latest "$sdk\build-tools").FullName
$plat  = (Latest "$sdk\platforms").FullName
$cmBin = "$((Latest "$sdk\cmake").FullName)\bin"
$java  = @("$env:JAVA_HOME\bin\java.exe") + (Get-ChildItem "C:\Program Files\Android\Android Studio*\jbr\bin\java.exe", "C:\Program Files\Java\jdk-*\bin\java.exe" -ErrorAction SilentlyContinue | ForEach-Object FullName) |
         Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $java) { throw "No JDK found (set JAVA_HOME)" }
$env:JAVA_HOME = Split-Path (Split-Path $java -Parent) -Parent  # apksigner.bat uses it
$adb   = "$sdk\platform-tools\adb.exe"
$strip = "$ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe"
"SDK $sdk`nNDK $(Split-Path $ndk -Leaf), build-tools $(Split-Path $bt -Leaf), $(Split-Path $plat -Leaf), JDK $env:JAVA_HOME"

# ---- native library per ABI ----
$stage = Join-Path $out "apk"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
foreach ($abi in $Abis) {
    $bd = Join-Path $out $abi
    "`n== $abi =="
    Run "$cmBin\cmake.exe" -G Ninja -S $code -B $bd "-DCMAKE_MAKE_PROGRAM=$cmBin\ninja.exe" `
        "-DCMAKE_TOOLCHAIN_FILE=$ndk\build\cmake\android.toolchain.cmake" "-DANDROID_ABI=$abi" "-DANDROID_PLATFORM=android-$minSdk" `
        "-DANDROID_STL=c++_static" "-DCMAKE_BUILD_TYPE=$Config" -Wno-dev | Select-Object -Last 1
    Run "$cmBin\cmake.exe" --build $bd | Select-Object -Last 1
    New-Item -ItemType Directory -Force "$stage\lib\$abi" | Out-Null
    Run $strip --strip-unneeded -o "$stage\lib\$abi\libmain.so" "$bd\libmain.so"
}

# ---- package ----
"`n== APK =="
$resZip = Join-Path $out "res.zip"; $unsigned = Join-Path $out "unaligned.apk"; $aligned = Join-Path $out "aligned.apk"; $apk = Join-Path $out "StarSwarm.apk"
Run "$bt\aapt2.exe" compile --dir "$PSScriptRoot\res" -o $resZip
Run "$bt\aapt2.exe" link -o $unsigned --manifest "$PSScriptRoot\AndroidManifest.xml" -I "$plat\android.jar" $resZip --min-sdk-version $minSdk
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($unsigned, "Update")
try {   # native libraries stored uncompressed so they can be loaded straight from the APK (zipalign pages them)
    foreach ($so in Get-ChildItem -Recurse "$stage\lib" -Filter *.so) {
        $entry = "lib/$($so.Directory.Name)/$($so.Name)"
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $so.FullName, $entry, [System.IO.Compression.CompressionLevel]::NoCompression)
    }
} finally { $zip.Dispose() }
Run "$bt\zipalign.exe" -f -P 16 4 $unsigned $aligned                # 16 KB page alignment for the .so files

$ks = "$env:USERPROFILE\.android\debug.keystore"
if (-not (Test-Path $ks)) {
    New-Item -ItemType Directory -Force (Split-Path $ks) | Out-Null
    Run "$env:JAVA_HOME\bin\keytool.exe" -genkeypair -keystore $ks -storepass android -keypass android -alias androiddebugkey `
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
}
Run "$bt\apksigner.bat" sign --ks $ks --ks-pass pass:android --key-pass pass:android --out $apk $aligned
Remove-Item $unsigned, $aligned, $resZip -ErrorAction SilentlyContinue
"APK: $apk ($([int]((Get-Item $apk).Length / 1KB)) KB, $($Abis -join ', '))"

if ($Install -or $Run) {
    Run $adb install -r $apk
    if ($Run) { Run $adb shell am start -n "$pkg/android.app.NativeActivity" }
}
