@echo off
title Mise a jour GitHub - Case92Telemetry
color 0b

echo ========================================
echo   Envoi des modifications sur GitHub...
echo ========================================
echo.

set /p msg="Entre un message pour ce commit (ex: correction bug) : "
if "%msg%"=="" set msg=Mise a jour du code

echo.
echo [1/3] Enregistrement de tes modifications...
git add -A
git commit -m "%msg%"

echo [2/3] Verification et synchronisation avec GitHub...
git pull -u origin dev --force

echo [3/3] Envoi final sur GitHub...
git push origin main

echo.
echo ========================================
echo   Termine ! Ton code est mis a jour.
echo ========================================
pause