# Packages the built DLLs into foo_dsp_fsurround.fb2k-component
#   32-bit DLL -> package root, 64-bit DLL -> x64\ (the layout foobar2000 expects)
# Run from the folder that contains foo_dsp_fsurround.vs2022.sln after building Release|Win32 and Release|x64.
param(
  [string]$Win32Dll = "bin\Win32\Release\foo_dsp_fsurround.dll",
  [string]$X64Dll   = "bin\x64\Release\foo_dsp_fsurround.dll",
  [string]$Out      = "out"
)
$ErrorActionPreference = "Stop"
$name  = "foo_dsp_fsurround"
$stage = Join-Path $Out $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$found = 0
if (Test-Path $Win32Dll) { Copy-Item $Win32Dll $stage; $found++ }
else { Write-Warning "Not found: $Win32Dll (package will have no 32-bit DLL)" }
if (Test-Path $X64Dll) {
  $x64 = Join-Path $stage "x64"; New-Item -ItemType Directory -Force -Path $x64 | Out-Null
  Copy-Item $X64Dll $x64; $found++
} else { Write-Warning "Not found: $X64Dll (package will have no 64-bit DLL)" }
if ($found -eq 0) { throw "No DLLs found - build the solution first." }

$zip = Join-Path $Out "$name.zip"; $pkg = Join-Path $Out "$name.fb2k-component"
Remove-Item $zip, $pkg -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip   # Compress-Archive only accepts .zip
Move-Item $zip $pkg
Write-Host "Created $pkg"
