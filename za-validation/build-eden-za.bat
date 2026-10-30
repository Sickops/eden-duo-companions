@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cmake --build eden-duo/build --target yuzu --parallel 6 > companions/za-validation/eden-build-v5.log 2>&1

