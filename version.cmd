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

@rem Don't include n and commit if we match a tag exactly.
if "%n%" == "0" set description=%major%.%minor%

@rem Ignore the build number if this isn't Jenkins.
if "%BUILD_NUMBER%" == "" set BUILD_NUMBER=0

@rem Create version.h.
@echo>version.h #define NSSM_VERSION _T("%description%")
@echo>>version.h #define NSSM_VERSIONINFO %major%,%minor%,%n%,%BUILD_NUMBER%
@echo>>version.h #define NSSM_DATE _T("%DATE%")
