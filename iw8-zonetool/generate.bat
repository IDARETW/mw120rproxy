@echo off
setlocal
where xmake >nul 2>nul || (
  echo xmake was not found in PATH.
  exit /b 1
)
xmake f -p windows -a x64 -m release -y || exit /b 1
xmake || exit /b 1
endlocal
