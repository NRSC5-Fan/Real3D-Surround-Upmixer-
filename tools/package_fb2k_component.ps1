# Packages the built DLLs into foo_dsp_real3d.fb2k-component
#   32-bit DLL -> package root, 64-bit DLL -> x64/ (the layout foobar2000 expects)
# Entries are written with forward slashes (Compress-Archive in Windows PowerShell 5.1 writes backslashes,
# which foobar2000 does not recognise as the x64 folder).
# Run from the folder that contains the .sln after building Release|Win32 and Release|x64.
param(
  [string]$Win32Dll = "bin\Win32\Release\foo_dsp_real3d.dll",
  [string]$X64Dll   = "bin\x64\Release\foo_dsp_real3d.dll",
  [string]$Out      = "out"
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$name = "foo_dsp_real3d"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$pkg = Join-Path (Resolve-Path $Out) "$name.fb2k-component"
Remove-Item $pkg -ErrorAction SilentlyContinue
$zip = [IO.Compression.ZipFile]::Open($pkg, [IO.Compression.ZipArchiveMode]::Create)
try {
  $found = 0
  foreach ($e in @(@($Win32Dll, "$name.dll"), @($X64Dll, "x64/$name.dll"))) {
    if (Test-Path $e[0]) { [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Resolve-Path $e[0]).Path, $e[1], [IO.Compression.CompressionLevel]::Optimal); $found++ }
    else { Write-Warning "Not found: $($e[0]) (package will not contain $($e[1]))" }
  }
  if ($found -eq 0) { throw "No DLLs found - build first." }
} finally { $zip.Dispose() }
Write-Host "Created $pkg"
