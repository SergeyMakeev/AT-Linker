@echo off
set builddir=build2026
if not exist %builddir% goto GENERATE
del %builddir% /S /Q
:GENERATE
mkdir %builddir%
cd %builddir%
cmake -G "Visual Studio 18 2026" ../
cd ..

