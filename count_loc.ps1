$total = 0
$c_cpp = 0
$rust = 0
$scripts = 0

$extensions = @('.cpp', '.hpp', '.c', '.h', '.rs', '.py', '.js', '.ps1', '.cmake')
$excludeDirs = @('.git', '.vscode', '.agent', 'assets', 'dist', 'build', 'logs', 'fonts', 'test', 'target', 'node_modules')

function Get-LineCount {
    param ($file)
    try {
        (Get-Content $file.FullName -ErrorAction Stop | Measure-Object -Line).Lines
    }
    catch {
        0
    }
}

Get-ChildItem -Path . -Recurse -File | ForEach-Object {
    $file = $_
    $path = $file.FullName
    
    # Check exclusion
    $excluded = $false
    foreach ($dir in $excludeDirs) {
        if ($path -match "\\$dir\\") {
            $excluded = $true
            break
        }
    }
    
    if (-not $excluded) {
        $lines = 0
        if ($file.Extension -in $extensions -or $file.Name -eq 'CMakeLists.txt') {
            $lines = Get-LineCount $file
            $total += $lines
            
            if ($file.Extension -in @('.cpp', '.hpp', '.c', '.h')) {
                $c_cpp += $lines
            }
            elseif ($file.Extension -eq '.rs') {
                $rust += $lines
            }
            elseif ($file.Extension -in @('.py', '.js', '.ps1', '.cmake') -or $file.Name -eq 'CMakeLists.txt') {
                $scripts += $lines
            }
        }
    }
}

Write-Output "C/C++: $c_cpp"
Write-Output "Rust: $rust"
Write-Output "Scripts: $scripts"
Write-Output "Total: $total"
