@rem Set default version in case git isn't available.
set description=0.0-0-prerelease
@rem Get canonical version from git tags, eg v2.21-24-g2c60e53.
for /f %%v in ('git describe --tags --long') do set description=%%v

@rem Strip leading v if present, eg 2.21-24-g2c60e53.
set description=%description:v=%
set version=%description%

@rem Get the number of commits and commit hash, eg 24-g2c60e53.
set n=%version:*-=%
set commit=%n:*-=%
call set n=%%n:%commit%=%%
set n=%n:~0,-1%

@rem Strip n and commit, eg 2.21.
call set version=%%version:%n%-%commit%=%%
set version=%version:~0,-1%

@rem Find major and minor.
set minor=%version:*.=%
call set major=%%version:.%minor%=%%

@rem Build flags.
set flags=0L

@rem Don't include n and commit if we match a tag exactly.
if "%n%" == "0" (set description=%major%.%minor%) else set flags=VS_FF_PRERELEASE
@rem Maybe we couldn't get the git tag.
if "%commit%" == "prerelease" set flags=VS_FF_PRERELEASE

@rem Ignore the build number if this isn't Jenkins.
if "%BUILD_NUMBER%" == "" set BUILD_NUMBER=0

@rem Copyright year provided by Jenkins.
set md=%BUILD_ID:*-=%
call set year=%%BUILD_ID:%md%=%%
set year=%year:~0,-1%
if "%BUILD_ID%" == "" set year=

@rem Create version.h.
@echo>version.h.new #define NSSM_VERSION _T("%description%")
@echo>>version.h.new #define NSSM_VERSIONINFO %major%,%minor%,%n%,%BUILD_NUMBER%
@echo>>version.h.new #define NSSM_DATE _T("%DATE%")
@echo>>version.h.new #define NSSM_FILEFLAGS %flags%
@echo>>version.h.new #define NSSM_COPYRIGHT _T("Public Domain; Author Iain Patterson 2003-%year%")

fc version.h version.h.new >NUL: 2>NUL:
if %ERRORLEVEL% == 0 (del version.h.new) else (move /y version.h.new version.h)
