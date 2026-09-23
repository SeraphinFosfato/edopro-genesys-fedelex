<#
.SYNOPSIS
    Costruisce ygopro.exe su Windows vero, con MSVC (D183).

.DESCRIPTION
    Uso: tools/release/build_windows.ps1 [release|debug]

    Perche' questo script esiste accanto a build_windows.sh. Il cross-build da
    Linux con MinGW (build_windows.sh) arriva al link e si ferma su 8 simboli
    che vengono tutti dalla cache vcpkg precompilata di terzi: e' costruita con
    una versione di mingw-w64 che i pacchetti di Ubuntu non riproducono. Tutti
    gli ostacoli incontrati per strada — le maiuscole degli header, il modello
    di thread, la versione del CRT, la cache incompatibile — sono artefatti del
    cross-compilare, e qui non esistono. Inseguire la versione di mingw di
    qualcun altro e' un bersaglio mobile: si ripara oggi e si rompe il giorno
    che la ricompilano.

    Cosa NON cambia rispetto al percorso cross, e va notato perche' e' il
    motivo per cui questo passaggio non tocca cio' che si distribuisce:
    l'eseguibile si chiama sempre ygopro.exe, esce sempre in bin/<config>/, e'
    sempre x86, ha sempre il core collegato staticamente dentro (nessuna
    ocgcore.dll a fianco) e ha anche il runtime collegato staticamente
    (premake5.lua: staticruntime "on"), quindi non chiede nessun redistribuibile
    sulla macchina di chi lo scarica. Cambia il compilatore, non il file.

    Tutti i valori qui sotto sono stati RICAVATI dai file che premake genera,
    non assunti: configurazione "Release|Win32", progetto "ygopro", OutDir
    "..\bin\release\". Era la divergenza fra un percorso scritto a mano e
    quello vero a tenere questa build rotta per mesi.

    NOTA: questo script non e' mai stato eseguito su un runner Windows al
    momento in cui e' stato scritto — chi lo lancia per la prima volta legga il
    report della sessione. La verifica finale (verify_windows_exe.py) c'e'
    proprio perche' nessuno possa scambiare un'uscita con 0 per un binario.
#>

[CmdletBinding()]
param(
    [ValidateSet('release', 'debug')]
    [string]$Config = 'release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# PowerShell non si ferma da solo quando un programma esterno fallisce: il suo
# codice di uscita va guardato a mano, ogni volta. Saltarne uno solo rimette
# in piedi esattamente il difetto che questa fase esiste per chiudere.
function Invoke-Passo {
    param([string]$Descrizione, [scriptblock]$Comando)
    Write-Host "==> $Descrizione"
    & $Comando
    if ($LASTEXITCODE -ne 0) {
        throw "$Descrizione — uscito con $LASTEXITCODE"
    }
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path
Set-Location $RepoRoot

$PremakeVersion = '5.0.0-beta2'
# Il tag "latest" della cache di edo9300. La variante -vs2022 e' quella giusta
# per windows-latest, che monta Visual Studio 2022: prendere quella senza
# suffisso (costruita con un altro Visual Studio) rimette in piedi lo stesso
# disallineamento di toolchain che ci ha fermati in cross-build.
$VcpkgCacheUrl = 'https://github.com/edo9300/edopro-vcpkg-cache/releases/latest/download/installed_x86-windows-static-vs2022.zip'

# ------------------------------------------------------------------ premake --

if (-not (Test-Path (Join-Path $RepoRoot 'premake5.exe'))) {
    Write-Host "==> Scarico premake5 v$PremakeVersion (pinnato, richiesto da questo progetto)"
    $zip = Join-Path $env:TEMP "premake-$PremakeVersion-windows.zip"
    Invoke-WebRequest -Uri "https://github.com/premake/premake-core/releases/download/v$PremakeVersion/premake-$PremakeVersion-windows.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $RepoRoot -Force
    Remove-Item $zip -Force
}

# ----------------------------------------------------------------- irrlicht --

# In un checkout pulito `irrlicht/` contiene SOLO i due file premake: il
# .gitignore esclude tutto il resto (`/irrlicht/*`). I sorgenti veri arrivano
# dal fork di Irrlicht di edo9300 e vanno copiati dentro, esattamente come fa
# build_windows.sh. Verificato su `git ls-files irrlicht`, che elenca due file:
# senza questo passo premake genera un progetto Irrlicht senza sorgenti.
$IrrlichtSrc = Join-Path (Split-Path -Parent $RepoRoot) 'irrlicht-custom'
if (-not (Test-Path $IrrlichtSrc)) {
    Invoke-Passo 'Clono edo9300/irrlicht1-8-4 (1.9-custom)' {
        & git clone --branch 1.9-custom --depth 1 https://github.com/edo9300/irrlicht1-8-4.git $IrrlichtSrc
    }
}
if (-not (Test-Path (Join-Path $RepoRoot 'irrlicht\include')) -or
    -not (Test-Path (Join-Path $RepoRoot 'irrlicht\src'))) {
    Write-Host '==> Popolo irrlicht\include e irrlicht\src'
    foreach ($d in 'irrlicht\include', 'irrlicht\src') {
        $p = Join-Path $RepoRoot $d
        if (Test-Path $p) { Remove-Item $p -Recurse -Force }
    }
    Copy-Item (Join-Path $IrrlichtSrc 'include') (Join-Path $RepoRoot 'irrlicht\include') -Recurse
    Copy-Item (Join-Path $IrrlichtSrc 'source\Irrlicht') (Join-Path $RepoRoot 'irrlicht\src') -Recurse
    # Queste quattro arrivano da vcpkg, come fa la CI di upstream: lasciare
    # anche le copie interne di Irrlicht significa due versioni della stessa
    # libreria nello stesso eseguibile.
    foreach ($d in 'bzip2', 'jpeglib', 'libpng', 'zlib') {
        $p = Join-Path $RepoRoot "irrlicht\src\$d"
        if (Test-Path $p) { Remove-Item $p -Recurse -Force }
    }
}

# -------------------------------------------------------------------- vcpkg --

# Le dipendenze arrivano dalla cache precompilata di edo9300, come fa la CI di
# upstream: costruire da sorgente i port vcpkg e' proprio cio' che rende
# doloroso questo pezzo. L'integrazione MSBuild di vcpkg e' il modo in cui i
# .vcxproj le trovano — premake ci scrive dentro <VcpkgTriplet>x86-windows-static.
$VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not $VcpkgRoot) { $VcpkgRoot = 'C:\vcpkg' }
$VcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'

if (-not (Test-Path (Join-Path $VcpkgRoot 'installed\x86-windows-static'))) {
    Write-Host '==> Scarico la cache vcpkg precompilata (x86-windows-static, vs2022)'
    $zip = Join-Path $env:TEMP 'vcpkg_cache.zip'
    Invoke-WebRequest -Uri $VcpkgCacheUrl -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $VcpkgRoot -Force
    Remove-Item $zip -Force
}
if (-not (Test-Path (Join-Path $VcpkgRoot 'installed\x86-windows-static'))) {
    throw "La cache vcpkg non contiene installed\x86-windows-static dentro $VcpkgRoot — senza, il link non si chiude."
}

Invoke-Passo 'Integro vcpkg in MSBuild' { & $VcpkgExe integrate install }

# ------------------------------------------------------------------ msbuild --

# vswhere sta sempre accanto all'installer di Visual Studio: si chiede a lui
# dov'e' MSBuild invece di aggiungere un'azione di terze parti al workflow o di
# cablare un percorso che cambia a ogni aggiornamento del runner.
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $VsWhere)) {
    throw "vswhere non trovato in $VsWhere — questo runner non ha Visual Studio."
}
$MsBuild = & $VsWhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $MsBuild -or -not (Test-Path $MsBuild)) {
    throw 'MSBuild.exe non trovato: vswhere non ha restituito nessun percorso.'
}
Write-Host "==> MSBuild: $MsBuild"

# -------------------------------------------------------------------- build --

# Si riparte sempre puliti: i generatori non trattano un cambio di opzioni come
# motivo per ricompilare, solo le date dei file. Un obj vecchio di un giro
# precedente si porta dietro flag che non ci sono piu' (gia' successo due volte
# sul percorso Linux: il percorso di irrlicht, poi il flag rpath).
foreach ($dir in 'build', 'obj', 'bin') {
    $p = Join-Path $RepoRoot $dir
    if (Test-Path $p) { Remove-Item $p -Recurse -Force }
}

# --sound=sfml e non "miniaudio,sfml": e' lo stesso backend che usa
# build_linux.sh, e i due client restano la stessa cosa compilata due volte.
# --no-direct3d perche' il DirectX SDK non c'e' sui runner (e la build Irrlicht
# si ferma con "DXSDK_DIR envvar not set"). Niente --no-core: il core va
# collegato dentro l'eseguibile, ed e' per questo che servono i submodule.
Invoke-Passo 'Genero la soluzione Visual Studio' {
    & (Join-Path $RepoRoot 'premake5.exe') vs2022 --no-direct3d --sound=sfml --no-joystick=true
}

$Sln = Join-Path $RepoRoot 'build\ygo.sln'
if (-not (Test-Path $Sln)) { throw "premake non ha generato $Sln." }

# "Release|Win32" e il progetto "ygopro" sono letti dalla soluzione generata,
# non scelti: sono gli unici nomi che premake emette per questo target.
$MsConfig = if ($Config -eq 'release') { 'Release' } else { 'Debug' }
Invoke-Passo "Compilo (Configuration=$MsConfig, Platform=Win32, target ygopro)" {
    & $MsBuild $Sln -m -t:ygopro -p:Configuration=$MsConfig -p:Platform=Win32 -verbosity:minimal -p:EchoOff=true
}

# ----------------------------------------------------------------- verifica --

# D178, la meta' che conta: un compilatore uscito con 0 non e' una prova che il
# binario esista. Il controllo e' in uno script a parte apposta per poterlo far
# fallire a comando — su Windows non c'e' `file`, quindi l'intestazione PE si
# legge direttamente.
$Exe = Join-Path $RepoRoot "bin\$Config\ygopro.exe"
$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) { $Python = Get-Command python3 -ErrorAction SilentlyContinue }
if (-not $Python) { throw 'Manca python, che serve a verificare il binario prodotto.' }

Invoke-Passo 'Verifico che sia davvero un eseguibile Windows' {
    & $Python.Source (Join-Path $ScriptDir 'verify_windows_exe.py') $Exe
}

Write-Host "Fatto: bin\$Config\ygopro.exe"
