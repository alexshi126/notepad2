@ECHO OFF
@rem used for GitHub Actions
SETLOCAL ENABLEEXTENSIONS

@rem https://github.com/actions/virtual-environments/blob/main/images/win/Windows2019-Readme.md#msys2
SET "PATH=C:\msys64\usr\bin;%PATH%"

@rem we don't need gdb or lldb, which saved the time to install Python and it's dependencies.

IF /I "%~1" == "x86_64" (
  SHIFT
  pacman -S --needed --noconfirm --noprogressbar mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-tools-git mingw-w64-x86_64-clang mingw-w64-x86_64-lld
)

IF /I "%~1" == "i686" (
  SHIFT
  pacman -S --needed --noconfirm --noprogressbar mingw-w64-i686-gcc mingw-w64-i686-make mingw-w64-i686-tools-git mingw-w64-i686-clang mingw-w64-i686-lld
)

IF /I "%~1" == "llvm" (
  SHIFT
  @rem for CI purpose only, the result binary is dynamic linked against api-ms-win-crt*.dll instead of msvcrt.dll
  SET "FILE=llvm-mingw-20201020-ucrt-x86_64.zip"
  curl -fsSL -o "%FILE%" "https://github.com/mstorsjo/llvm-mingw/releases/download/20201020/%FILE%"
  7z x -y -o"C:\" "%FILE%" >NUL
)
