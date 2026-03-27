param(
    [switch]$Check
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Push-Location $repoRoot

try {
    function Resolve-RequiredCommand {
        param(
            [string[]]$Candidates,
            [string]$Hint
        )

        foreach ($candidate in $Candidates) {
            $command = Get-Command $candidate -ErrorAction SilentlyContinue
            if ($command) {
                return $command.Source
            }
        }

        throw "Missing required command. $Hint"
    }

    $clangFormat = Resolve-RequiredCommand -Candidates @("clang-format") `
        -Hint "Install clang-format and ensure it is on PATH."
    $pythonCommand = Resolve-RequiredCommand -Candidates @("python", "py") `
        -Hint "Install Python and ensure 'python' or 'py' is on PATH."

    $clangArgs = if ($Check) { @("--dry-run", "--Werror") } else { @("-i") }
    $prettierMode = if ($Check) { "--check" } else { "--write" }
    $blackArgs = @("-m", "black")
    $ruffArgs = @("-m", "ruff", "format")

    if ($pythonCommand -like "*py.exe") {
        $pythonPrefix = @("-3")
    } else {
        $pythonPrefix = @()
    }

    if ($Check) {
        $blackArgs += "--check"
        $ruffArgs += "--check"
    }

    $cppFiles = Get-ChildItem Backend -Recurse -Include *.cpp, *.h | ForEach-Object { $_.FullName }
    foreach ($file in $cppFiles) {
        & $clangFormat @clangArgs $file
    }

    $prettierTargets = @(
        "README.md",
        ".prettierrc.json",
        "ZImageFrontend/package.json",
        "ZImageFrontend/jsconfig.json"
    )
    $prettierTargets += Get-ChildItem ZImageFrontend/src -Recurse -Include *.js, *.vue |
        ForEach-Object { $_.FullName }

    & npx --yes prettier@3.6.2 $prettierMode @prettierTargets
    & $pythonCommand @pythonPrefix @blackArgs "ModelService"
    & $pythonCommand @pythonPrefix @ruffArgs "ModelService"
} finally {
    Pop-Location
}
