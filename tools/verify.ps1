# VoxOne regression gate for L0, M0, S0, DIN0 and W0.
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
$partitionsPath=Join-Path $root 'partitions_yoradio.csv'
$partitionRows=Import-Csv -LiteralPath $partitionsPath -Header Name,Type,SubType,Offset,Size,Flags
function Get-PartitionSize([string]$name) {
 $row=$partitionRows | Where-Object { $_.Name.Trim() -eq $name } | Select-Object -First 1
 if(-not $row) { throw "Partition not found: $name" }
 $value=$row.Size.Trim()
 if($value -match '^0[xX][0-9a-fA-F]+$') { return [Convert]::ToInt64($value.Substring(2),16) }
 return [Convert]::ToInt64($value,10)
}
$appPartitionSize=Get-PartitionSize 'app0'
if((Get-PartitionSize 'app1') -ne $appPartitionSize) { throw 'app0 and app1 partition sizes differ' }
$versionHeader=Get-Content -LiteralPath (Join-Path $root 'src/core/version.h') -Raw
$versionMatch=[regex]::Match($versionHeader,'(?m)^\s*#define\s+VOXONE_VERSION\s+"(\d+\.\d+\.\d+)"\s*$')
if(-not $versionMatch.Success) { throw 'VOXONE_VERSION must be SemVer in src/core/version.h' }
$productVersion=$versionMatch.Groups[1].Value
$results=[ordered]@{}
$sizes=[ordered]@{}
$firmwareMemory=[ordered]@{}
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
 bt_firmware_sender_native=@('src/core/bt_firmware_sender.cpp','src/core/bt_link_protocol.cpp')
 bt_firmware_staging_native=@('src/core/bt_firmware_staging.cpp')
 bt_volume_native=@('src/core/bt_link_protocol.cpp')
 config_format_native=@('src/core/config_format.cpp','src/core/volume_map.cpp')
 config_backup_native=@('src/core/config_backup.cpp','src/core/config_format.cpp','src/core/volume_map.cpp','src/hardware/runtime_hardware_config.cpp')
 config_backup_web_native=@('src/core/config_backup_web.cpp')
 config_restore_transaction_native=@('src/core/config_restore_transaction.cpp')
 config_startup_native=@('src/core/config_format.cpp','src/core/volume_map.cpp')
 config_persistence_native=@('src/core/config_format.cpp','src/core/volume_map.cpp')
 dsp_model_native=@('src/core/dsp_model.cpp')
 dsp_storage_format_native=@('src/core/dsp_model.cpp','src/core/dsp_storage_format.cpp')
 dsp_service_native=@('src/core/dsp_model.cpp','src/core/dsp_service.cpp')
 dsp_transport_native=@('src/core/dsp_model.cpp','src/core/dsp_storage_format.cpp','src/core/dsp_service.cpp','src/core/dsp_transport_protocol.cpp')
 dsp_state_json_native=@('src/core/dsp_model.cpp','src/core/dsp_storage_format.cpp','src/core/dsp_service.cpp','src/core/dsp_state_json.cpp')
 mqtt_config_native=@('src/core/volume_map.cpp')
 radio_format_detection_native=@('src/core/radio_format_detection.cpp')
 radio_url_resolution_native=@('src/core/radio_url_resolution.cpp')
 runtime_display_activation_native=@('src/displays/display_backend_selection.cpp','src/hardware/runtime_hardware_config.cpp')
 runtime_hardware_config_native=@('src/hardware/runtime_hardware_config.cpp')
 runtime_hardware_web_native=@('src/hardware/runtime_hardware_config.cpp','src/hardware/runtime_hardware_web.cpp')
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
  $variants=if($name -eq 'config_backup_native' -or $name -eq 'hardware_descriptor_native' -or $name -eq 'runtime_hardware_config_native' -or $name -eq 'runtime_hardware_web_native' -or $name -eq 'runtime_display_activation_native') { @('L0','M0','S0','DIN0','W0') } elseif($name -eq 'build_identity_native') { @('','L0','M0','S0','DIN0','W0','CLEAN','DIRTY') } else { @('') }
  foreach($variant in $variants) {
   $label=if($variant) { "$name-$variant" } else { $name }
   $exe=Join-Path $work ($label+$(if($onWindows) { '.exe' } else { '' }))
   $compileLog=Join-Path $work ($label+'.compile.log')
   $compileArgs=@('-std=c++11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter')
   if($variant) {
    if($variant -eq 'CLEAN' -or $variant -eq 'DIRTY') { $compileArgs+="-DBUILD_ID_TEST_$variant=1" }
    else { $compileArgs+="-DVOXONE_PROFILE_$variant=1" }
    $sourcesForVariant=if($name -eq 'config_backup_native' -or $name -eq 'hardware_descriptor_native' -or $name -eq 'runtime_hardware_config_native' -or $name -eq 'runtime_hardware_web_native' -or $name -eq 'runtime_display_activation_native') { $sources+@(Join-Path $root 'src/hardware/hardware_descriptor.cpp') } else { $sources }
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
if(-not $onWindows) {
 Invoke-Step 'Config restore sanitizers' {
  $sanitizerArgs=@('-std=c++11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-Wno-unused-parameter')
  $backupExe=Join-Path $work 'config_backup_sanitized'
  $backupSources=@(
   (Join-Path $root 'test/config_backup_native.cpp'),
   (Join-Path $root 'src/core/config_backup.cpp'),
   (Join-Path $root 'src/core/config_format.cpp'),
   (Join-Path $root 'src/core/volume_map.cpp'),
   (Join-Path $root 'src/hardware/runtime_hardware_config.cpp'),
   (Join-Path $root 'src/hardware/hardware_descriptor.cpp'))
  & $Cxx @sanitizerArgs '-DVOXONE_PROFILE_L0=1' '-o' $backupExe @backupSources
  if($LASTEXITCODE -ne 0) { throw 'Sanitized config backup compile failed' }
  & $backupExe
  if($LASTEXITCODE -ne 0) { throw 'Sanitized config backup test failed' }
  $restoreExe=Join-Path $work 'config_restore_sanitized'
  & $Cxx @sanitizerArgs '-o' $restoreExe `
    (Join-Path $root 'test/config_restore_transaction_native.cpp') `
    (Join-Path $root 'src/core/config_restore_transaction.cpp')
  if($LASTEXITCODE -ne 0) { throw 'Sanitized config restore compile failed' }
  & $restoreExe
  if($LASTEXITCODE -ne 0) { throw 'Sanitized config restore test failed' }
 }
}
Invoke-Step 'Web Update headless' {
 & $Python (Join-Path $root 'test/voxone_update_headless.py') 2>&1 | ForEach-Object { Write-Host $_ }
 if($LASTEXITCODE -ne 0) { throw 'Existing Chrome test failed' }
}
$initialAssets=Get-AssetHashes
Invoke-Step 'SPIFFS' {
 Invoke-Pio 'l0-buildfs' @('run','-e','l0','-t','buildfs')
 $image=Join-Path $root '.pio/build/l0/spiffs.bin'
 if(-not (Test-Path -LiteralPath $image)) { throw 'Missing spiffs.bin' }
 $sizes['SPIFFS']=(Get-Item -LiteralPath $image).Length
}
Invoke-Step 'WWW assets' {
 Assert-WebAssets
 $script:generatedAssets=Get-AssetHashes
 $changed=@($initialAssets.Keys | Where-Object { $initialAssets[$_] -ne $script:generatedAssets[$_] })
 if($changed.Count -gt 0) { Write-Host ("  normalized generated assets: " + ($changed -join ', ')) }
}
foreach($target in @('l0','m0','s0','din0','w0')) {
 $envName=$target
 Invoke-Step ($envName.ToUpperInvariant()) {
  Invoke-Pio "$envName-firmware" @('run','-e',$envName)
  $bin=Join-Path $root ".pio/build/$envName/firmware.bin"
  if(-not (Test-Path -LiteralPath $bin)) { throw "Missing $envName firmware.bin" }
  $versionedBin=Join-Path $root ".pio/build/$envName/VoxOne-$productVersion-firmware.bin"
  if(-not (Test-Path -LiteralPath $versionedBin)) { throw "Missing versioned firmware: $versionedBin" }
  if((Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $versionedBin -Algorithm SHA256).Hash) { throw "Versioned firmware differs: $versionedBin" }
  $profile=$envName.ToUpperInvariant()
  $firmwareSize=(Get-Item -LiteralPath $bin).Length
  if($firmwareSize -gt $appPartitionSize) { throw "$profile firmware exceeds app partition" }
  $sizes[$profile]=$firmwareSize
  $firmwareMemory[$profile]=[pscustomobject]@{
   Firmware=$firmwareSize
   UsedPercent=100.0*$firmwareSize/$appPartitionSize
   Free=$appPartitionSize-$firmwareSize
  }
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
Write-Host ("App partition size: {0} B" -f $appPartitionSize)
foreach($name in $firmwareMemory.Keys) {
 $memory=$firmwareMemory[$name]
 $used=$memory.UsedPercent.ToString('F2',[Globalization.CultureInfo]::InvariantCulture)
 Write-Host ("{0} firmware: {1} B, used: {2}%, free: {3} B" -f $name,$memory.Firmware,$used,$memory.Free)
}
if($script:failed) { Write-Host 'RESULT: FAIL'; exit 1 }
Write-Host 'RESULT: PASS'
exit 0

\n
