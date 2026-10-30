@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cl /nologo /std:c++20 /EHsc /O2 /I"D:\Projet IA\Dual screen\eden-duo\src" companions\eden-duo-companions-super-mario-odyssey-1.0.0\tools\za\eden_memory_probe.cpp companions\eden-duo-companions-super-mario-odyssey-1.0.0\native\modules\za_save.cpp /Fe:companions\za-validation\eden_memory_probe_v7.exe /Fo:companions\za-validation\

