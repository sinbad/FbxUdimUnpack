# Simple tool to monitor a directory and automatically run udimunpack on changed files
[CmdletBinding()] # Fail on unknown args
param (
    [string]$sourcedir,
    [string]$destdir,
    [switch]$watch,
    [switch]$help = $false
)

function Write-Usage {
    Write-Output "UDIM Automation Tool"
    Write-Output "Usage:"
    Write-Output "  auto-udim-unpack.ps1 sourcedir destdir"
    Write-Output " "
    Write-Output "  sourcedir  : Folder to monitor for FBX files"
    Write-Output "  destdir    : Folder to write converted FBX files"
    Write-Output "  -watch     : Instead of just checking folders once, keep running and monitor"
    Write-Output "  -verbose   : Verbose mode"
    Write-Output "  -help      : Print this help"
}

# Find Unpack tool
# Simplify platform checks for Powershell < 6
if (-not $PSVersionTable.Platform) {
    # This is Windows-only powershell
    $global:IsWindows = $true
}


$exeSuffix = ""
if ($IsWindows) {
    $exeSuffix = ".exe"
}

$UnpackExe = "UdimUnpack$exeSuffix"
# Check we can call UdimUnpack
if (-not (Get-Command $UnpackExe -ErrorAction SilentlyContinue)) {
    # if not on path, try local version
    $UnpackExe = Join-Path $PSScriptRoot $UnpackExe
    if (-not (Get-Command $UnpackExe -ErrorAction SilentlyContinue)) {
        Write-Output "ERROR: Cannot find UdimUnpack tool on PATH or in script dir"
        Exit 5
    }
}


function UnpackSingle {
    [CmdletBinding()]
    param (
        [string] $source,
        [string] $dest,
        [switch] $checkTimestamp = $false
    )

    # Make sure destination exists
    $destdir = Split-Path $dest -Parent
    if (-not (Test-Path $destdir -PathType Container)) {
        Write-Verbose "Creating directory $destdir"
        New-Item -ItemType Directory -Path $destdir -Force > $null
    }

    if ($checkTimestamp -and
        (Test-Path $dest -ItemType Leaf)) {

        $srctime =  (Get-ItemProperty $source -name LastWriteTime).LastWriteTime
        $desttime =  (Get-ItemProperty $dest -name LastWriteTime).LastWriteTime
        if ($desttime -ge $srctime) {
            # Up to date
            Write-Verbose "$outfile is up to date"
            return
        }
    }


    Write-Verbose "Running: UdimUnpack '$source' '$dest'"
    $argList = [System.Collections.ArrayList]@()
    # always output something in output dir
    $argList.Add("--always") > $null
    $argList.Add($source) > $null
    $argList.Add($dest) > $null

    Start-Process $UnpackExe $argList -Wait -PassThru -NoNewWindow
}

function UnpackDir {
    [CmdletBinding()]
    param (
        [string] $sourcedir,
        [string] $destdir
    )

    Get-ChildItem $sourcedir -Filter *.fbx | ForEach-Object {
        $infile = $_.FullName
        $outfile = Join-Path $destdir $_.Name
        if (Test-Path $outfile -PathType Leaf) {
            # check datetime
            $intime =  $_.LastWriteTime
            $outtime =  (Get-ItemProperty $outfile -name LastWriteTime).LastWriteTime
            if ($intime -le $outtime) {
                # Up to date
                Write-Verbose "$outfile is up to date"
                # DO NOT use "continue" in a ForEach-Object, it's not a for!
                # Continue will actually break out of the whole program
                return
            }
        }

        # If we got here outfile is either missing or out of date
        UnpackSingle $infile $outfile
    }   
    
}

function UnpackRecurse {
    [CmdletBinding()]
    param (
        [string] $sourcedir,
        [string] $destdir
    )

    UnpackDir $sourcedir $destdir

    Get-ChildItem $sourcedir -Directory | ForEach-Object {
        UnpackRecurse $_.FullName (Join-Path $destdir $_.Name)
    }
}

$ErrorActionPreference = "Stop"

if ($help) {
    Write-Usage
    Exit 0
}

if (-not $sourcedir) {
    Write-Usage
    Write-Output "ERROR: You must specify a source folder"
    Exit 1
}
if (-not $destdir) {
    Write-Usage
    Write-Output "ERROR: You must specify a source folder"
    Exit 1
}

if (-not (Test-Path $sourcedir -PathType Container)) {
    Write-Output "ERROR: sourcedir $sourcedir is invalid"
    Exit 1
}

# Scan existing contents looking for anything out of date
UnpackRecurse $sourcedir $destdir

if ($watch) {
    # Now set up a file watcher
    $watcher = New-Object IO.FileSystemWatcher $sourcedir, *.fbx -Property @{IncludeSubdirectories = $true; NotifyFilter = [IO.NotifyFilters]'FileName, LastWrite'} 

    Register-ObjectEvent $watcher Created -SourceIdentifier FileCreated -Action { 
        $filename = $Event.SourceEventArgs.Name
        $infile = Join-Path $sourcedir $filename
        $outfile = Join-Path $destdir $filename
        Write-Host "Processing new file: $filename"
        # Check the timestamps anyway to protect against duplicate events
        UnpackSingle $infile $outfile -checkTimestamp:$true
    } > $null
         
    Register-ObjectEvent $watcher Changed -SourceIdentifier FileChanged -Action { 
        $filename = $Event.SourceEventArgs.Name
        $infile = Join-Path $sourcedir $filename
        $outfile = Join-Path $destdir $filename
        Write-Host "Processing change: $filename"
        # Check the timestamps anyway to protect against duplicate events
        UnpackSingle $infile $outfile -checkTimestamp:$true
    } > $null

    Write-Output "Monitoring $sourcedir"
    Write-Output "Press Ctrl+C to exit"
    [console]::TreatControlCAsInput = $true
    while ($true) {        
        if ([console]::KeyAvailable)
        {
            $key = [system.console]::readkey($true)
            if (($key.modifiers -band [consolemodifiers]"control") -and ($key.key -eq "C"))
            {
                Write-Output "Shutting Down..."
                break
            }
        } else {
            Start-Sleep 1
        }
    }

    Unregister-Event FileCreated
    Unregister-Event FileChanged



}
