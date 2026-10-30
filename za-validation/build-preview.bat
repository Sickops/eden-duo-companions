@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cmake --build eden-duo/build --target dsmod_preview --parallel 6 > companions/za-validation/preview-build.log 2>&1
