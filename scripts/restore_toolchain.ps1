$ErrorActionPreference = "Stop"

$url = "https://github.com/brechtsanders/winlibs_mingw/releases/download/14.2.0-17.0.6-12.0.0-ucrt-r1/winlibs-x86_64-posix-seh-gcc-14.2.0-llvm-17.0.6-mingw-w64ucrt-12.0.0-r1.zip"
$output = "mingw64.zip"
$dest = "../toolchain/compiler"

Write-Host "Downloading MinGW-w64 (GCC 14.2.0 UCRT)..."
Invoke-WebRequest -Uri $url -OutFile $output

if (!(Test-Path $dest)) {
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
}

Write-Host "Extracting..."
Expand-Archive -Path $output -DestinationPath $dest -Force

Write-Host "Cleaning up..."
Remove-Item $output

Write-Host "Done! MinGW restored to $dest\mingw64"
Write-Host "You may need to restart your shell."
