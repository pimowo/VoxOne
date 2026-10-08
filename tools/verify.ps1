# VoxOne regression gate for desk, din and salon.
[CmdletBinding()]
param(
 [string]$PlatformIo,
 [string]$Cxx='g++',
 [string]$Python
)
$ErrorActionPreference='Stop'
$onWindows=$env:OS -eq 'Windows_NT'
function Resolve-Executable([string]$label,[string]$value,[bool]$explicit,[string[]]$names,[string]$windowsFallback) {
 if($explicit) {
  if([string]::IsNullOrWhiteSpace($value) -or -not (Get-Command -Name $value -CommandType Application -ErrorAction SilentlyContinue)) {
   throw "$label not found: $value"
  }
  return $value
 }
 foreach($name in $names) {
  $command=Get-Command -Name $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
  if($command) { return $command.Source }
 }
 if($onWindows -and $windowsFallback -and (Test-Path -LiteralPath $windowsFallback -PathType Leaf)) { return $windowsFallback }
 throw "$label not found. Tried: $($names -join ', ')$(if($onWindows) { ", $windowsFallback" })"
}
$PlatformIo=Resolve-Executable 'PlatformIO CLI' $PlatformIo $PSBoundParameters.ContainsKey('PlatformIo') @('pio','platformio') 'C:\Users\piotrek\.platformio\penv\Scripts\pio.exe'
$pythonNames=if($onWindows) { @('python3','python','py') } else { @('python3','python') }
$Python=Resolve-Executable 'Python' $Python $PSBoundParameters.ContainsKey('Python') $pythonNames 'C:\Users\piotrek\.platformio\penv\Scripts\python.exe'
$Cxx=Resolve-Executable 'C++ compiler' $Cxx $PSBoundParameters.ContainsKey('Cxx') @('g++') ''
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $root
$work=Join-Path $root '.pio/verify'
[void][System.IO.Directory]::CreateDirectory($work)
$results=[ordered]@{}
$sizes=[ordered]@{}
$script:failed=$false
function Invoke-Step([string]$name,[scriptblock]$action) {
 Write-Host "RUN $name"
 try { & $action; $results[$name]='PASS'; Write-Host "PASS $name" }
 catch { $results[$name]='FAIL'; $script:failed=$true; Write-Host ("FAIL {0}: {1}" -f $name,$_.Exception.Message) }
}
function Invoke-Pio([string]$label,[string[]]$arguments) {
 $log=Join-Path $work ($label+'.log')
 $oldPreference=$ErrorActionPreference
 $ErrorActionPreference='Continue'
 try { & $PlatformIo @arguments *> $log; $code=$LASTEXITCODE }
 finally { $ErrorActionPreference=$oldPreference }
 if($code -ne 0) { Get-Content -LiteralPath $log -Tail 50 | Out-Host; throw "PlatformIO $label exit $code; log $log" }
 if(-not (Select-String -LiteralPath $log -Pattern '\[SUCCESS\]' -Quiet)) { throw "No SUCCESS marker: $log" }
 Write-Host "  log: $log"
}
$assetNames=@('voxone.html','voxone.css','voxone.js','advanced-audio.js','dsp-client.js','voxone-logo.svg')
function Get-AssetHashes {
 $hashes=[ordered]@{}
 foreach($name in $assetNames) {
  $path=Join-Path $root "data/www/$name.gz"
  if(-not (Test-Path -LiteralPath $path)) { throw "Missing $path" }
  $hashes[$name]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
 }
 return $hashes
}
function Assert-WebAssets {
 foreach($name in $assetNames) {
  $source=[System.IO.File]::ReadAllBytes((Join-Path $root "web-src/$name"))
  if($name -eq 'voxone.html') {
   $html=[System.Text.Encoding]::UTF8.GetString($source)
   foreach($other in $assetNames | Where-Object { $_ -ne 'voxone.html' }) {
    $bytes=[System.IO.File]::ReadAllBytes((Join-Path $root "web-src/$other"))
    $sha=[System.Security.Cryptography.SHA256]::Create()
    try { $digest=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','').ToLowerInvariant().Substring(0,12) }
    finally { $sha.Dispose() }
    $html=$html.Replace("{$other-hash}",$digest)
   }
   $source=[System.Text.Encoding]::UTF8.GetBytes($html)
  }
  $inputStream=[System.IO.File]::OpenRead((Join-Path $root "data/www/$name.gz"))
  $gzip=New-Object System.IO.Compression.GZipStream($inputStream,[System.IO.Compression.CompressionMode]::Decompress)
  $output=New-Object System.IO.MemoryStream
  try { $gzip.CopyTo($output); $actual=$output.ToArray() }
  finally { $output.Dispose(); $gzip.Dispose(); $inputStream.Dispose() }
  if($actual.Length -ne $source.Length) { throw "Asset size mismatch: $name" }
  for($i=0;$i -lt $source.Length;$i++) { if($actual[$i] -ne $source[$i]) { throw "Asset content mismatch: $name" } }
 }
 Write-Host '  six gzip assets match web-src'
}
$nativeSources=@{
 bt_link_native=@('src/core/bt_link_protocol.cpp')
 bt_volume_native=@('src/core/bt_link_protocol.cpp')
 config_format_native=@('src/core/config_format.cpp','src/core/volume_map.cpp')
 config_startup_native=@('src/core/config_format.cpp','src/core/volume_map.cpp')
 config_persistence_native=@('src/core/config_format.cpp','src/core/volume_map.cpp')
 dsp_model_native=@('src/core/dsp_model.cpp')
 dsp_storage_format_native=@('src/core/dsp_model.cpp','src/core/dsp_storage_format.cpp')
 dsp_service_native=@('src/core/dsp_model.cpp','src/core/dsp_service.cpp')
 dsp_transport_native=@('src/core/dsp_model.cpp','src/core/dsp_storage_format.cpp','src/core/dsp_service.cpp','src/core/dsp_transport_protocol.cpp')
 dsp_state_json_native=@('src/core/dsp_model.cpp','src/core/dsp_storage_format.cpp','src/core/dsp_service.cpp','src/core/dsp_state_json.cpp')
 mqtt_config_native=@('src/core/volume_map.cpp')
 station_directory_native=@('src/core/station_directory_format.cpp')
 volume_limits_native=@('src/core/volume_map.cpp')
}
$tests=@(Get-ChildItem -LiteralPath (Join-Path $root 'test') -Filter '*_native.cpp' -File | Sort-Object Name)
Invoke-Step 'Native tests' {
 if($tests.Count -eq 0) { throw 'No native tests found' }
 foreach($test in $tests) {
  $name=$test.BaseName
  $sources=@($test.FullName)
  if($nativeSources.ContainsKey($name)) { $sources+=@($nativeSources[$name] | ForEach-Object { Join-Path $root $_ }) }
  $variants=if($name -eq 'hardware_descriptor_native') { @('DESK','DIN','SALON') } else { @('') }
  foreach($variant in $variants) {
   $label=if($variant) { "$name-$variant" } else { $name }
   $exe=Join-Path $work ($label+$(if($onWindows) { '.exe' } else { '' }))
   $compileLog=Join-Path $work ($label+'.compile.log')
   $compileArgs=@('-std=c++11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter')
   if($variant) {
    $compileArgs+="-DVOXONE_PROFILE_$variant=1"
    $sourcesForVariant=$sources+@(Join-Path $root 'src/hardware/hardware_descriptor.cpp')
   } else { $sourcesForVariant=$sources }
   $compileArgs+=@('-o',$exe)+$sourcesForVariant
   $oldPreference=$ErrorActionPreference
   $ErrorActionPreference='Continue'
   try { & $Cxx @compileArgs *> $compileLog; $compileCode=$LASTEXITCODE }
   finally { $ErrorActionPreference=$oldPreference }
   if($compileCode -ne 0) { Get-Content -LiteralPath $compileLog -Tail 30 | Out-Host; throw "Compile failed: $label" }
   $testLog=Join-Path $work ($label+'.log')
   & $exe *> $testLog
   if($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $testLog -Tail 20 | Out-Host; throw "Test failed: $label" }
   Write-Host "  PASS $label"
  }
 }
 Write-Host "  $($tests.Count) native tests passed"
}
Invoke-Step 'Web Update headless' {
 & $Python (Join-Path $root 'test/voxone_update_headless.py') 2>&1 | ForEach-Object { Write-Host $_ }
 if($LASTEXITCODE -ne 0) { throw 'Existing Chrome test failed' }
}
$initialAssets=Get-AssetHashes
Invoke-Step 'SPIFFS' {
 Invoke-Pio 'salon-buildfs' @('run','-e','salon','-t','buildfs')
 $image=Join-Path $root '.pio/build/salon/spiffs.bin'
 if(-not (Test-Path -LiteralPath $image)) { throw 'Missing spiffs.bin' }
 $sizes['SPIFFS']=(Get-Item -LiteralPath $image).Length
}
Invoke-Step 'WWW assets' {
 Assert-WebAssets
 $script:generatedAssets=Get-AssetHashes
 $changed=@($initialAssets.Keys | Where-Object { $initialAssets[$_] -ne $script:generatedAssets[$_] })
 if($changed.Count -gt 0) { Write-Host ("  normalized generated assets: " + ($changed -join ', ')) }
}
foreach($target in @('desk','din','salon')) {
 $envName=$target
 Invoke-Step ($envName.ToUpperInvariant()) {
  Invoke-Pio "$envName-firmware" @('run','-e',$envName)
  $bin=Join-Path $root ".pio/build/$envName/firmware.bin"
  if(-not (Test-Path -LiteralPath $bin)) { throw "Missing $envName firmware.bin" }
  $sizes[$envName.ToUpperInvariant()]=(Get-Item -LiteralPath $bin).Length
 }
}
Invoke-Step 'WWW repeatability' {
 Assert-WebAssets
 $afterBuilds=Get-AssetHashes
 foreach($name in $script:generatedAssets.Keys) { if($script:generatedAssets[$name] -ne $afterBuilds[$name]) { throw "Generated gzip changed after build: $name" } }
}
Invoke-Step 'git diff --check' {
 & git diff --check
 if($LASTEXITCODE -ne 0) { throw 'Whitespace errors' }
}
Write-Host ''
Write-Host 'VoxOne Regression Verification'
foreach($name in $results.Keys) { Write-Host ("{0}: {1}" -f $name,$results[$name]) }
foreach($name in $sizes.Keys) { Write-Host ("{0} size: {1} B" -f $name,$sizes[$name]) }
if($script:failed) { Write-Host 'RESULT: FAIL'; exit 1 }
Write-Host 'RESULT: PASS'
exit 0

\n
