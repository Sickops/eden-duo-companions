@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cl /nologo /std:c++20 /EHsc /O2 companions\za-validation\inspect_crash.cpp /Fe:companions\za-validation\inspect_crash.exe /Fo:companions\za-validation\inspect_crash.obj
