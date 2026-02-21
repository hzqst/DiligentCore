@echo off

setlocal

:: Check if SolutionDir is already set and non-empty
if not defined SolutionDir (
    :: Only set SolutionDir if it's not already set
    SET "SolutionDir=%~dp0"
)

:: Ensure the path ends with a backslash
if not "%SolutionDir:~-1%"=="\" SET "SolutionDir=%SolutionDir%\"

cd /d "%SolutionDir%"

call cmake -G "Visual Studio 17 2022" -B "%SolutionDir%build\x64\Debug" -A x64 -DCMAKE_INSTALL_PREFIX="%SolutionDir%install\x64\Debug" -DDILIGENT_BUILD_FX=TRUE -DDILIGENT_BUILD_SAMPLES=TRUE -DDILIGENT_BUILD_TOOLS=TRUE -DDILIGENT_NO_WEBGPU=FALSE -DDILIGENT_NO_ARCHIVER=FALSE -DDILIGENT_BUILD_TESTS=TRUE -DDILIGENT_DEVELOPMENT=TRUE -DDILIGENT_NO_FORMAT_VALIDATION=FALSE -DDILIGENT_USE_SPIRV_TOOLCHAIN=TRUE

call cmake --build "%SolutionDir%build\x64\Debug" --config Debug --target install

endlocal