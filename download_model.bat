@echo off
chcp 65001 > nul
echo ========================================
echo CloseCrab-Unified Model Downloader
echo ========================================
echo.

echo [Step 1] Select LLM model:
echo.
echo [1] Qwen2.5-7B  (Recommended, 4.5GB, 8GB VRAM)
echo [2] Qwen2.5-14B (Stronger, 8.5GB, 12GB VRAM)
echo [3] Qwen2.5-3B  (Light, 2GB, 6GB VRAM)
echo [4] Qwen2.5-1.5B(Fast, 1.2GB, 4GB VRAM)
echo.
set /p choice="Enter number (1-4): "

if "%choice%"=="1" (
    set LLM_URL=https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/main/qwen2.5-7b-instruct-q4_k_m.gguf
    set LLM_NAME=qwen2.5-7b-instruct-q4_k_m.gguf
)
if "%choice%"=="2" (
    set LLM_URL=https://huggingface.co/Qwen/Qwen2.5-14B-Instruct-GGUF/resolve/main/qwen2.5-14b-instruct-q4_k_m.gguf
    set LLM_NAME=qwen2.5-14b-instruct-q4_k_m.gguf
)
if "%choice%"=="3" (
    set LLM_URL=https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf
    set LLM_NAME=qwen2.5-3b-instruct-q4_k_m.gguf
)
if "%choice%"=="4" (
    set LLM_URL=https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf
    set LLM_NAME=qwen2.5-1.5b-instruct-q4_k_m.gguf
)

mkdir models 2>nul
echo.
echo Downloading %LLM_NAME%...
curl -L --retry 3 --progress-bar -o "models\%LLM_NAME%" "%LLM_URL%"
if %errorlevel% equ 0 ( echo [OK] Done ) else ( echo [FAIL] Download failed )

echo.
echo ========================================
echo Model saved to: models\%LLM_NAME%
echo Update config\config.yaml if needed.
echo ========================================
pause
