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

function UnpackAll {
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
                continue
            }
        }
    
        # If we got here outfile is either missing or out of date
        Write-Output "Running: UdimUnpack '$infile' '$outfile'"
        UdimUnpack "$infile" "$outfile"
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
if (-not (Test-Path $destdir -PathType Container)) {
    Write-Output "WARNING: creating destination dir $destdir"
    New-Item -ItemType Directory -Path $destdir -Force > $null
}

# Check we can call UdimUnpack
try {
    UdimUnpack --help > $null
} catch {
    Write-Output "ERROR: UdimUnpack is not on the PATH"
    Exit 3
}

# Scan existing contents looking for anything out of date
UnpackAll $sourcedir $destdir

if ($watch) {
    # Now set up a file watcher
    # TODO

}
