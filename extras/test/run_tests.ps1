# Builds and runs the PacketLED host tests (requires: python -m pip install ziglang).
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $env:TEMP "packetled_test.exe"
python -m ziglang c++ -std=c++17 -O2 -Wall -Wextra -I "$root\src" "$PSScriptRoot\test_packetled.cpp" "$root\src\PacketLED.cpp" -o $exe
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed"; exit 1 }
& $exe
exit $LASTEXITCODE
