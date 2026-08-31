@echo off
REM ============================================================
REM  打包 CAN_Host_Tool.exe（单文件，输出到桌面）
REM  前提：本机已安装「完整版 Python（含 tkinter）」
REM    - 本环境默认路径：C:\Users\qin\AppData\Local\Programs\Python\Python313\python.exe
REM    - 若路径不同，请修改下面的 PY 变量
REM  保持 --console：运行时附带黑色控制台窗口，便于看串口日志
REM  若要纯净无控制台：把 --console 改为 --noconsole
REM ============================================================
setlocal
cd /d "%~dp0"

set "PY=C:\Users\qin\AppData\Local\Programs\Python\Python313\python.exe"
if not exist "%PY%" (
    echo [ERROR] 未找到完整版 Python，请修改 build_exe.bat 中的 PY 路径
    echo         需要含 tkinter 的 Python（精简版 managed Python 不行）
    pause
    exit /b 1
)

"%PY%" -m pip install --no-input pyinstaller pyserial
"%PY%" -m PyInstaller --onefile --console --name CAN_Host_Tool ^
    --distpath "%USERPROFILE%\Desktop" ^
    --clean host_tool.py

echo.
echo [DONE] 产物：%USERPROFILE%\Desktop\CAN_Host_Tool.exe
pause
