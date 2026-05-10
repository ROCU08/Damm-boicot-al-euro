@echo off
setlocal enabledelayedexpansion

:: Validación: Si el primer parámetro está vacío, ir a USAGE
if "%1"=="" goto USAGE

:: Mapeo manual
set DIA=
if "%1"=="1" set DIA=lunes
if "%1"=="2" set DIA=martes
if "%1"=="3" set DIA=miercoles
if "%1"=="4" set DIA=jueves
if "%1"=="5" set DIA=viernes
if "%1"=="6" set DIA=pruebas_SA

:: Si el número no es válido (DIA sigue vacío)
if "%DIA%"=="" (
    echo Error: Numero invalido.
    goto USAGE
)

echo ==============================================
echo EJECUTANDO TEST UNICO: %DIA%
echo ==============================================

make clean
make damm damm-dist
make pipeline JUEGO=test/juego_%DIA%.csv OUT_DIR=out_%DIA%

if exist frontend (
    cd frontend
    call npm install
    echo Lanzando npm run dev para %DIA%...
    start "Frontend %DIA%" npm run dev
    cd ..
)
goto :EOF

:USAGE
echo Uso: %~nx0 [numero]
echo   1 - lunes
echo   2 - martes
echo   3 - miercoles
echo   4 - jueves
echo   5 - viernes
exit /b 1