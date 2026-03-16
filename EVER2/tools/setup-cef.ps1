param(
    [string]$ProjectDir,
    [string]$Platform,
    [string]$Configuration
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
    throw "ProjectDir is required."
}

$ProjectDir = $ProjectDir.Trim()
if ($ProjectDir.Contains('" -Platform "')) {
    $ProjectDir = $ProjectDir.Split('" -Platform "')[0]
}
if ($ProjectDir.Contains('" -Configuration "')) {
    $ProjectDir = $ProjectDir.Split('" -Configuration "')[0]
}
$ProjectDir = $ProjectDir.Trim('"')
$ProjectDir = $ProjectDir.TrimEnd('\\')

if ($Platform -and $Platform -ne "x64") {
    Write-Host "[EVER2] Skipping CEF setup for platform '$Platform'."
    exit 0
}

$projectRoot = (Resolve-Path -Path $ProjectDir).Path
$thirdPartyDir = Join-Path $projectRoot "third_party"
$cefRoot = Join-Path $thirdPartyDir "cef"
$cacheDir = Join-Path $thirdPartyDir "_cache"
$stampPath = Join-Path $cefRoot ".ever2-cef-version"

$cefDownloadBase = "https://cef-builds.spotifycdn.com"
$cefIndexUrl = "$cefDownloadBase/index.html"
$archivePattern = 'cef_binary_[^"''\s]+_windows64\.tar\.bz2'
$pinnedArchiveUrl = "https://runtime.fivem.net/build/cef/cef_binary_103.0.0-cfx-m103.2605+g1316dae+chromium-103.0.5060.141_windows64_minimal.zip"

if ($env:CEF_MINIMAL_URL -and -not [string]::IsNullOrWhiteSpace($env:CEF_MINIMAL_URL)) {
    $pinnedArchiveUrl = $env:CEF_MINIMAL_URL
}

function Ensure-Dir([string]$path) {
    if (-not (Test-Path $path)) {
        New-Item -ItemType Directory -Path $path | Out-Null
    }
}

function Write-Stamp([string]$archiveName) {
    $stampContent = @(
        "archive=$archiveName"
        "updated_utc=$([DateTime]::UtcNow.ToString('o'))"
    )
    Set-Content -Path $stampPath -Value $stampContent -Encoding ascii
}

function Get-LatestArchiveName {
    Write-Host "[EVER2] Fetching CEF build index..."
    $index = Invoke-WebRequest -Uri $cefIndexUrl -UseBasicParsing
    $matches = [regex]::Matches($index.Content, $archivePattern) | ForEach-Object { $_.Value } | Sort-Object -Unique
    if (-not $matches -or $matches.Count -eq 0) {
        throw "No windows64 CEF archive found at $cefIndexUrl"
    }

    return ($matches | Sort-Object)[-1]
}

function Get-ArchiveNameFromUrl([string]$url) {
    $uri = [System.Uri]$url
    return [System.Uri]::UnescapeDataString([System.IO.Path]::GetFileName($uri.AbsolutePath))
}

function Install-CefFromArchive([string]$archiveName, [string]$archiveUrl = $null) {
    Ensure-Dir $cacheDir

    if ([string]::IsNullOrWhiteSpace($archiveUrl)) {
        $archiveUrl = "$cefDownloadBase/$archiveName"
    }
    $archivePath = Join-Path $cacheDir $archiveName

    if (-not (Test-Path $archivePath)) {
        Write-Host "[EVER2] Downloading $archiveName"
        Invoke-WebRequest -Uri $archiveUrl -OutFile $archivePath -UseBasicParsing
    } else {
        Write-Host "[EVER2] Using cached archive $archiveName"
    }

    if (Test-Path $cefRoot) {
        Remove-Item -Path $cefRoot -Recurse -Force
    }

    Ensure-Dir $cefRoot

    $extractRoot = Join-Path $cacheDir ("extract_" + [IO.Path]::GetFileNameWithoutExtension([IO.Path]::GetFileNameWithoutExtension($archiveName)))
    if (Test-Path $extractRoot) {
        Remove-Item -Path $extractRoot -Recurse -Force
    }
    Ensure-Dir $extractRoot

    Write-Host "[EVER2] Extracting archive..."
    tar -xf $archivePath -C $extractRoot

    $extractedDirs = Get-ChildItem -Path $extractRoot -Directory
    if (-not $extractedDirs -or $extractedDirs.Count -eq 0) {
        throw "CEF archive extraction failed: no root directory found."
    }

    $cefExtractedRoot = $extractedDirs[0].FullName

    $requiredPaths = @("include", "Release", "Resources", "libcef_dll")
    foreach ($rel in $requiredPaths) {
        $src = Join-Path $cefExtractedRoot $rel
        if (-not (Test-Path $src)) {
            throw "CEF archive missing required path: $rel"
        }
        Copy-Item -Path $src -Destination (Join-Path $cefRoot $rel) -Recurse -Force
    }

    Write-Stamp $archiveName
}

Ensure-Dir $thirdPartyDir

if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
    throw "'tar' command is required but not available on PATH. Install BSdtar/Windows tar support."
}

if ($env:CEF_BINARY_ROOT -and (Test-Path $env:CEF_BINARY_ROOT)) {
    Write-Host "[EVER2] CEF_BINARY_ROOT is set. Mirroring binary SDK into third_party/cef"

    if (Test-Path $cefRoot) {
        Remove-Item -Path $cefRoot -Recurse -Force
    }
    Ensure-Dir $cefRoot

    $requiredPaths = @("Release", "Resources")
    foreach ($rel in $requiredPaths) {
        $src = Join-Path $env:CEF_BINARY_ROOT $rel
        if (-not (Test-Path $src)) {
            throw "CEF_BINARY_ROOT missing required folder: $rel"
        }
        Copy-Item -Path $src -Destination (Join-Path $cefRoot $rel) -Recurse -Force
    }

    $headerSrc = Join-Path $projectRoot "third_party\cef\include"
    if (-not (Test-Path $headerSrc)) {
        $fallbackHeader = Join-Path $env:CEF_BINARY_ROOT "include"
        if (-not (Test-Path $fallbackHeader)) {
            throw "No CEF include headers found in project or CEF_BINARY_ROOT/include"
        }
        Copy-Item -Path $fallbackHeader -Destination (Join-Path $cefRoot "include") -Recurse -Force
    }

    Write-Stamp "env:CEF_BINARY_ROOT"
    Write-Host "[EVER2] CEF SDK prepared from CEF_BINARY_ROOT."
    exit 0
}

$latestArchive = Get-ArchiveNameFromUrl $pinnedArchiveUrl
$latestArchiveUrl = $pinnedArchiveUrl

try {
    Write-Host "[EVER2] Using pinned CEF archive URL: $pinnedArchiveUrl"
    Invoke-WebRequest -Uri $pinnedArchiveUrl -Method Head -UseBasicParsing | Out-Null
} catch {
    Write-Host "[EVER2] Pinned URL unavailable, falling back to index discovery."
    $latestArchive = Get-LatestArchiveName
    $latestArchiveUrl = "$cefDownloadBase/$latestArchive"
}

$needsInstall = $true
if (Test-Path $stampPath) {
    $stamp = Get-Content $stampPath -Raw
    if ($stamp -match [regex]::Escape("archive=$latestArchive")) {
        $pathsOk = (Test-Path (Join-Path $cefRoot "include\cef_app.h")) -and
                   (Test-Path (Join-Path $cefRoot "Release\libcef.lib")) -and
                   (Test-Path (Join-Path $cefRoot "Release\libcef.dll"))
        if ($pathsOk) {
            $needsInstall = $false
        }
    }
}

if ($needsInstall) {
    Install-CefFromArchive $latestArchive $latestArchiveUrl
} else {
    Write-Host "[EVER2] CEF SDK already up to date ($latestArchive)."
}

Write-Host "[EVER2] CEF setup complete."
exit 0
