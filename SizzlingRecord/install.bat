@echo off
setlocal enableextensions enabledelayedexpansion

set PROJECT_DIR="%~dp0"
cd "%~dp0"
cd ..

if exist "TF2_DIR.txt" (
  set /p TF2_DIR= < "TF2_DIR.txt"
) else (
  set TF2_DIR=
)

if [!TF2_DIR!] == [] exit 1

@echo on
mkdir "!TF2_DIR!\addons\sizzlingplugins\sizzlingrecord"
copy /Y "%1" "!TF2_DIR!\addons\sizzlingplugins\sizzlingrecord\"
copy /Y "!PROJECT_DIR!/sizzlingrecord.vdf" "!TF2_DIR!\addons\"
