@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cd /d "%~dp0"
cl /nologo /std:c++20 /EHsc /O2 /MD /w /DZA_ASSETS_TRACE /I. /I"D:\Projet IA\Dual screen\eden-duo\externals\bc_decoder" /I"D:\Projet IA\Dual screen\eden-duo\externals\stb" za_assets.cpp za_resources_test.cpp "D:\Projet IA\Dual screen\eden-duo\externals\bc_decoder\bc_decoder.cpp" third_party\ooz\kraken.cpp third_party\ooz\lzna.cpp third_party\ooz\bitknit.cpp third_party\ooz\oozlib.cpp /Fo:za_resources_obj\ /Fe:za_resources_test.exe
