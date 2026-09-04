@ECHO OFF

pushd %~dp0

REM Command file for the Serial Modem documentation.
REM
REM The documentation is split into docsets, each built with its own
REM configuration from _docsets\ over the shared sources in this directory.
REM _scripts\build_docsets.py does the work; this file is a thin wrapper.

if "%PYTHON%" == "" (
	set PYTHON=python
)
if "%BUILDDIR%" == "" (
	set BUILDDIR=build
)

%PYTHON% --version >NUL 2>NUL
if errorlevel 1 (
	echo.
	echo.The 'python' command was not found. Make sure you have Python and the
	echo.packages from requirements.txt installed, then set the PYTHON
	echo.environment variable to point to the full path of the 'python'
	echo.executable. Alternatively you may add Python to PATH.
	echo.
	exit /b 1
)

set BUILDDOCSETS=%PYTHON% _scripts\build_docsets.py -b "%BUILDDIR%" %SPHINXOPTS% %O%

if "%1" == "" goto help
if "%1" == "help" goto help
if "%1" == "clean" goto clean
if "%1" == "html" goto docsets
if "%1" == "docsets" goto docsets

REM Any other argument is treated as a single docset name.
%BUILDDOCSETS% --docset %1
goto end

:docsets
%BUILDDOCSETS%
goto end

:clean
if exist "%BUILDDIR%" rmdir /s /q "%BUILDDIR%"
goto end

:help
echo.Run 'doxygen' first, then one of:
echo.  html       Build all docsets into %BUILDDIR%\html (same as docsets)
echo.  docsets    Build all docsets into %BUILDDIR%\html
echo.  main       Build only the main docset
echo.  nrf91m1    Build only the nRF91M1 docset
echo.  clean      Remove %BUILDDIR%
echo.
echo.Pass extra sphinx-build options with SPHINXOPTS.

:end
popd
