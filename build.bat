@echo off
echo Compiling CSIR Document Search Engine (C11)...
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude ^
    src/common.c src/corpus.c src/tokenizer.c src/index.c ^
    src/persistence.c src/search.c src/benchmark.c src/report.c src/main.c ^
    -o csir.exe -lm
if %ERRORLEVEL% NEQ 0 (
    echo Build FAILED!
    exit /b 1
)
echo Build succeeded: csir.exe

echo Compiling Test Suite...
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude ^
    src/common.c src/corpus.c src/tokenizer.c src/index.c ^
    src/persistence.c src/search.c src/benchmark.c src/report.c tests/test_suite.c ^
    -o test_suite.exe -lm
if %ERRORLEVEL% NEQ 0 (
    echo Test build FAILED!
    exit /b 1
)
echo Test suite built: test_suite.exe
