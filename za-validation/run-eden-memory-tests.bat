@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cmake --build eden-duo/build --target tests --parallel 4 > companions/za-validation/eden-tests-build-v6.log 2>&1
if errorlevel 1 exit /b 1
eden-duo\build\bin\tests.exe "DeviceMemoryManager*,MemoryTracker*" > companions/za-validation/eden-memory-tests-v6.log 2>&1
