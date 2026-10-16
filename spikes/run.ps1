# Compile and run a single reflection spike with GCC 16.
# Usage: pwsh -File run.ps1 s1_access.cpp
param([Parameter(Mandatory = $true)][string]$Src)

$gxx  = "D:\msys64\ucrt64\bin\g++.exe"
$name = [System.IO.Path]::GetFileNameWithoutExtension($Src)

& $gxx -std=c++26 -freflection -O0 -Wall -Wextra -o "$name.exe" $Src -lstdc++exp "-Wl,--allow-multiple-definition"
if ($LASTEXITCODE -ne 0) {
    Write-Host "COMPILE FAILED: $Src" -ForegroundColor Red
    exit $LASTEXITCODE
}

& ".\$name.exe"
Write-Host "RUN $name exit=$LASTEXITCODE" -ForegroundColor Green
