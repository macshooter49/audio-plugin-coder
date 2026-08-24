# Copy Agent Folders Script
# OLD: Copies .agent folder contents to .kilocode and .claude folders for multi-agent compatibility
# NEW: Copies .claude folder contents to .kilocode and .agent folders for multi-agent compatibility

param(
    [switch]$Force,
    [switch]$Verbose
)

# Set error action preference
$ErrorActionPreference = "Stop"

# Get script directory
$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

# Define paths
$ClaudeFolder   = Join-Path $ProjectRoot ".claude"
$KiloCodeFolder = Join-Path $ProjectRoot ".kilocode"
$AgentFolder    = Join-Path $ProjectRoot ".agent"

# Function to write verbose output
function Write-VerboseOutput {
    param([string]$Message)
    if ($Verbose) {
        Write-Host $Message -ForegroundColor Cyan
    }
}

# Function to copy folder contents
function Copy-ClaudeFolder {
    param(
        [string]$SourceFolder,
        [string]$DestinationFolder,
        [string]$AgentName
    )

    Write-VerboseOutput "Processing $AgentName folder..."

    # Check if source exists
    if (-not (Test-Path $SourceFolder)) {
        Write-Warning "Source folder '$SourceFolder' does not exist. Skipping $AgentName."
        return
    }

    # Check if destination exists
    if ((Test-Path $DestinationFolder) -and -not $Force) {
        $response = Read-Host "Destination folder '$DestinationFolder' already exists. Overwrite? (y/N)"
        if ($response -notin @('y','Y')) {
            Write-Host "Skipping $AgentName copy." -ForegroundColor Yellow
            return
        }
    }

    # Create destination if it doesn't exist
    if (-not (Test-Path $DestinationFolder)) {
        New-Item -ItemType Directory -Path $DestinationFolder -Force | Out-Null
        Write-VerboseOutput "Created destination folder: $DestinationFolder"
    }

    # Copy contents
    try {
        Copy-Item -Path "$SourceFolder\*" -Destination $DestinationFolder -Recurse -Force
        Write-Host "Successfully copied .claude contents to $AgentName folder." -ForegroundColor Green
        Write-VerboseOutput "Copied to: $DestinationFolder"

        # Update path references
        Update-PathReferences -DestinationFolder $DestinationFolder -AgentName $AgentName
    }
    catch {
        Write-Error "Failed to copy to $AgentName folder: $($_.Exception.Message)"
    }
}

# Function to update path references in copied files
function Update-PathReferences {
    param(
        [string]$DestinationFolder,
        [string]$AgentName
    )

    Write-VerboseOutput "Updating path references in $AgentName folder..."

    $DestFolderName = Split-Path $DestinationFolder -Leaf

    # Find all markdown files recursively
    $MdFiles = Get-ChildItem -Path $DestinationFolder -Filter "*.md" -Recurse

    foreach ($file in $MdFiles) {
        try {
            $content = Get-Content -Path $file.FullName -Raw
            $originalContent = $content

            # Replace .claude with destination folder name
            $content = $content -replace '\.claude', ".$DestFolderName"

            if ($content -ne $originalContent) {
                Set-Content -Path $file.FullName -Value $content -Encoding UTF8
                Write-VerboseOutput "Updated paths in: $($file.FullName)"
            }
        }
        catch {
            Write-Warning "Failed to update paths in $($file.FullName): $($_.Exception.Message)"
        }
    }

    Write-Host "Path references updated in $AgentName folder." -ForegroundColor Green
}

# Main execution
Write-Host "Claude Folder Copy Script" -ForegroundColor Magenta
Write-Host "=========================" -ForegroundColor Magenta
Write-Host ""

if ($Verbose) {
    Write-Host "Project Root:   $ProjectRoot" -ForegroundColor Gray
    Write-Host "Claude Folder:  $ClaudeFolder" -ForegroundColor Gray
    Write-Host "KiloCode Folder:$KiloCodeFolder" -ForegroundColor Gray
    Write-Host "Agent Folder:   $AgentFolder" -ForegroundColor Gray
    Write-Host ""
}

# Copy to KiloCode folder
Copy-ClaudeFolder -SourceFolder $ClaudeFolder -DestinationFolder $KiloCodeFolder -AgentName "KiloCode"

# Copy to Agent folder
Copy-ClaudeFolder -SourceFolder $ClaudeFolder -DestinationFolder $AgentFolder -AgentName "Agent"

Write-Host ""
Write-Host "Copy operation completed." -ForegroundColor Green

if ($Verbose) {
    Write-Host ""
    Write-Host "Summary:" -ForegroundColor Yellow
    Write-Host "- .claude -> .kilocode" -ForegroundColor Gray
    Write-Host "- .claude -> .agent" -ForegroundColor Gray
    Write-Host ""
    Write-Host "This allows the same skills, rules, guides, and workflows" -ForegroundColor Gray
    Write-Host "to work with different AI agents that expect different folder structures." -ForegroundColor Gray
}
