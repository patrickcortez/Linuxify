# Linuxify Installer Build Script
# This script builds the Linuxify installer

Write-Host "=== Linuxify Installer Builder ===" -ForegroundColor Green
Write-Host ""

# Check for Inno Setup
$InnoPath = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $InnoPath)) {
    $InnoPath = "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
}

if (-not (Test-Path $InnoPath)) {
    Write-Host "ERROR: Inno Setup 6 not found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install Inno Setup from:" -ForegroundColor Yellow
    Write-Host "https://jrsoftware.org/isdl.php" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "After installing, run this script again." -ForegroundColor Yellow
    exit 1
}

# Create output directory
$OutputDir = "installer\output"
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

# Check if ICO file exists, if not show instructions
$IcoPath = "assets\linuxify.ico"
if (-not (Test-Path $IcoPath)) {
    Write-Host "WARNING: ICO file not found at assets\linuxify.ico" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To create an ICO file from your PNG:" -ForegroundColor Cyan
    Write-Host "1. Go to https://convertio.co/png-ico/" -ForegroundColor White
    Write-Host "2. Upload assets\linux_penguin_animal_9362.png" -ForegroundColor White
    Write-Host "3. Download the ICO file" -ForegroundColor White
    Write-Host "4. Save it as assets\linuxify.ico" -ForegroundColor White
    Write-Host ""
    
    # For now, copy PNG as a placeholder (won't work as icon but allows build)
    Write-Host "Continuing without icon for now..." -ForegroundColor Yellow
}

# Compile the installer
Write-Host "Building installer..." -ForegroundColor Cyan
& $InnoPath "installer\linuxify.iss"

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "SUCCESS! Installer created at:" -ForegroundColor Green
    Write-Host "installer\output\LinuxifySetup.exe" -ForegroundColor Cyan
} else {
    Write-Host ""
    Write-Host "Build failed with exit code: $LASTEXITCODE" -ForegroundColor Red
}
