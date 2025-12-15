# Linuxify Development Libraries Installer
# Run: .\scripts\install-libs.ps1
# Downloads and installs common development libraries from MSYS2 into the MinGW toolchain

param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$TOOLCHAIN = "$PSScriptRoot\..\toolchain\compiler\mingw64"
$TEMP_DIR = "$env:TEMP\linuxify-libs"
$MIRROR = "https://mirror.msys2.org/mingw/mingw64"

$PACKAGES = @(
    @{ Name = "zlib"; File = "mingw-w64-x86_64-zlib-1.3.1-1-any.pkg.tar.zst" },
    @{ Name = "openssl"; File = "mingw-w64-x86_64-openssl-3.4.0-1-any.pkg.tar.zst" },
    @{ Name = "pdcurses"; File = "mingw-w64-x86_64-pdcurses-4.5.3-1-any.pkg.tar.zst" },
    @{ Name = "libpng"; File = "mingw-w64-x86_64-libpng-1.6.53-1-any.pkg.tar.zst" },
    @{ Name = "curl"; File = "mingw-w64-x86_64-curl-8.11.1-1-any.pkg.tar.zst" },
    @{ Name = "sqlite3"; File = "mingw-w64-x86_64-sqlite3-3.51.1-1-any.pkg.tar.zst" }
)

function Write-Status($msg) {
    Write-Host "[*] $msg" -ForegroundColor Cyan
}

function Write-Success($msg) {
    Write-Host "[+] $msg" -ForegroundColor Green
}

function Write-Warn($msg) {
    Write-Host "[!] $msg" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Magenta
Write-Host "  Linuxify Development Library Installer" -ForegroundColor Magenta
Write-Host "========================================" -ForegroundColor Magenta
Write-Host ""

$TOOLCHAIN = (Resolve-Path $TOOLCHAIN).Path
Write-Status "Toolchain: $TOOLCHAIN"

if (-not (Test-Path "$TOOLCHAIN\bin\gcc.exe")) {
    throw "MinGW toolchain not found at $TOOLCHAIN"
}

if (Test-Path $TEMP_DIR) {
    Remove-Item -Recurse -Force $TEMP_DIR
}
New-Item -ItemType Directory -Path $TEMP_DIR | Out-Null

foreach ($pkg in $PACKAGES) {
    $name = $pkg.Name
    $file = $pkg.File
    $url = "$MIRROR/$file"
    $dest = "$TEMP_DIR\$file"
    $extractDir = "$TEMP_DIR\$name"
    
    Write-Status "Downloading $name..."
    try {
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    }
    catch {
        Write-Warn "Failed to download $name from primary URL, trying alternate..."
        $altUrl = $url -replace "1\.3\.1-1", "1.3.1-2" -replace "3\.4\.0-1", "3.6.0-1" -replace "4\.4\.0-2", "4.5.3-1" -replace "1\.6\.44-1", "1.6.53-1" -replace "8\.11\.1-1", "8.17.0-1" -replace "3\.47\.2-1", "3.51.1-1"
        Invoke-WebRequest -Uri $altUrl -OutFile $dest -UseBasicParsing
    }
    
    Write-Status "Extracting $name..."
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null
    
    Push-Location $extractDir
    tar -xf $dest 2>$null
    Pop-Location
    
    $mingw64Dir = "$extractDir\mingw64"
    if (Test-Path $mingw64Dir) {
        Write-Status "Fixing pkg-config files for $name..."
        $pcFiles = Get-ChildItem -Path $mingw64Dir -Filter "*.pc" -Recurse -ErrorAction SilentlyContinue
        foreach ($pc in $pcFiles) {
            $content = Get-Content $pc.FullName -Raw
            $fixedContent = $content -replace 'prefix=/mingw64', 'prefix=${pcfiledir}/../..'
            $fixedContent = $fixedContent -replace 'prefix=/ucrt64', 'prefix=${pcfiledir}/../..'
            Set-Content -Path $pc.FullName -Value $fixedContent -NoNewline
        }
        
        $configScripts = @("curl-config", "libpng-config", "libpng16-config", "openssl-config")
        foreach ($script in $configScripts) {
            $scriptPath = "$mingw64Dir\bin\$script"
            if (Test-Path $scriptPath) {
                Write-Status "Fixing config script: $script"
                $content = Get-Content $scriptPath -Raw
                $fixedContent = $content -replace '/mingw64', '$( cd "$(dirname "$0")/.." && pwd )'
                $fixedContent = $fixedContent -replace '/ucrt64', '$( cd "$(dirname "$0")/.." && pwd )'
                Set-Content -Path $scriptPath -Value $fixedContent -NoNewline
            }
        }
        
        Write-Status "Installing $name to toolchain..."
        
        if (Test-Path "$mingw64Dir\include") {
            Copy-Item -Path "$mingw64Dir\include\*" -Destination "$TOOLCHAIN\include" -Recurse -Force
        }
        
        if (Test-Path "$mingw64Dir\lib") {
            Get-ChildItem "$mingw64Dir\lib" -File | ForEach-Object {
                Copy-Item $_.FullName -Destination "$TOOLCHAIN\lib" -Force
            }
            if (Test-Path "$mingw64Dir\lib\pkgconfig") {
                if (-not (Test-Path "$TOOLCHAIN\lib\pkgconfig")) {
                    New-Item -ItemType Directory -Path "$TOOLCHAIN\lib\pkgconfig" -Force | Out-Null
                }
                Copy-Item -Path "$mingw64Dir\lib\pkgconfig\*" -Destination "$TOOLCHAIN\lib\pkgconfig" -Force
            }
        }
        
        if (Test-Path "$mingw64Dir\bin") {
            Copy-Item -Path "$mingw64Dir\bin\*" -Destination "$TOOLCHAIN\bin" -Force
        }
        
        Write-Success "$name installed!"
    }
    else {
        Write-Warn "No mingw64 directory found in $name package"
    }
}

Write-Status "Fixing PDCurses header location..."
if (Test-Path "$TOOLCHAIN\include\pdcurses\curses.h") {
    Copy-Item "$TOOLCHAIN\include\pdcurses\curses.h" -Destination "$TOOLCHAIN\include\curses.h" -Force
    Write-Success "curses.h copied to standard include path"
}

Write-Status "Cleaning up..."
Remove-Item -Recurse -Force $TEMP_DIR

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Installation Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Installed libraries:" -ForegroundColor White
Write-Host "  - zlib      (-lz)" -ForegroundColor Gray
Write-Host "  - OpenSSL   (-lssl -lcrypto)" -ForegroundColor Gray
Write-Host "  - PDCurses  (-lpdcurses)" -ForegroundColor Gray
Write-Host "  - libpng    (-lpng)" -ForegroundColor Gray
Write-Host "  - libcurl   (-lcurl)" -ForegroundColor Gray
Write-Host "  - SQLite3   (-lsqlite3)" -ForegroundColor Gray
Write-Host ""
Write-Host "Test with: echo '#include <zlib.h>' | gcc -xc - -lz -o nul" -ForegroundColor Yellow
