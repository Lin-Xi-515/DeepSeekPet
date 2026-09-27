@echo off
rem 在桌面创建一个"DeepSeek 桌宠"快捷方式（可选，双击本文件即可）
setlocal
set "HERE=%~dp0"
set "EXE=%HERE%DeepSeekPet.exe"
if not exist "%EXE%" (
  echo [错误] 没找到 DeepSeekPet.exe，请把本文件放在与它相同的目录里。
  pause
  exit /b 1
)
powershell -NoProfile -Command ^
  "$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')+'\DeepSeek 桌宠.lnk');" ^
  "$s.TargetPath='%EXE%'; $s.WorkingDirectory='%HERE%'; $s.IconLocation='%EXE%,0'; $s.Description='DeepSeek 桌宠'; $s.Save()"
echo 已在桌面创建快捷方式：DeepSeek 桌宠
echo （如果桌宠不在托盘里，双击快捷方式即可启动）
pause
