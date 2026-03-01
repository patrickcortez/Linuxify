@echo off
cd /d "c:\Users\ADMIN\Documents\Projects\Linuxify\cmds-src"

echo Compiling ls.cpp...
g++ -std=c++17 -static -o ../cmds/ls.exe ls.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling mkdir.cpp...
g++ -std=c++17 -static -o ../cmds/mkdir.exe mkdir.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling rm.cpp...
g++ -std=c++17 -static -o ../cmds/rm.exe rm.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling mv.cpp...
g++ -std=c++17 -static -o ../cmds/mv.exe mv.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling cp.cpp...
g++ -std=c++17 -static -o ../cmds/cp.exe cp.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling touch.cpp...
g++ -std=c++17 -static -o ../cmds/touch.exe touch.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling chmod.cpp...
g++ -std=c++17 -static -o ../cmds/chmod.exe chmod.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling chown.cpp...
g++ -std=c++17 -static -o ../cmds/chown.exe chown.cpp -ladvapi32
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling find.cpp...
g++ -std=c++17 -static -o ../cmds/find.exe find.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling ln.cpp...
g++ -std=c++17 -static -o ../cmds/ln.exe ln.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling stat.cpp...
g++ -std=c++17 -static -o ../cmds/stat.exe stat.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling file.cpp...
g++ -std=c++17 -static -o ../cmds/file.exe file.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling readlink.cpp...
g++ -std=c++17 -static -o ../cmds/readlink.exe readlink.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling realpath.cpp...
g++ -std=c++17 -static -o ../cmds/realpath.exe realpath.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling basename.cpp...
g++ -std=c++17 -static -o ../cmds/basename.exe basename.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling dirname.cpp...
g++ -std=c++17 -static -o ../cmds/dirname.exe dirname.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling tree.cpp...
g++ -std=c++17 -static -o ../cmds/tree.exe tree.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compiling du.cpp...
g++ -std=c++17 -static -o ../cmds/du.exe du.cpp
if %errorlevel% neq 0 exit /b %errorlevel%

echo All Batch 1 commands compiled successfully!
