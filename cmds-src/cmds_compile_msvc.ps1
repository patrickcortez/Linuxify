$vsPath = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
if (-not $vsPath) {
    Write-Output "VS not found"
    exit 1
}
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

$cmds = @("ls", "mkdir", "rm", "mv", "cp", "touch", "chmod", "chown", "find", "ln", "stat", "file", "readlink", "realpath", "basename", "dirname", "tree", "du")

foreach ($cmd in $cmds) {
    Write-Output "Compiling $cmd.cpp"
    $clArgs = "cl /EHsc /std:c++17 /Fe:..\cmds\$cmd.exe $cmd.cpp"
    if ($cmd -eq "chown") {
        $clArgs += " advapi32.lib"
    }
    cmd.exe /c "`"$vcvars`" && $clArgs > compile_$cmd.log 2>&1"
}
Write-Output "All 18 commands compiled."
