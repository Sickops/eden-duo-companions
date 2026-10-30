@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cmake --build companions/build-native-za-test --target dsmod-za-save-test dsmod-za-live-test dsmod-za-module-host-test --parallel 6 > companions/za-validation/tests-build-v3.log 2>&1
if errorlevel 1 exit /b 1
ctest --test-dir companions/build-native-za-test -R "^dsmod-za-save$" --output-on-failure
