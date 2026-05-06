$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$WorkflowPath = Join-Path $RepoRoot ".github/workflows/ci.yml"
$PreflightPath = Join-Path $RepoRoot "tools/pre_push_windows_msvc.ps1"
$HookPath = Join-Path $RepoRoot ".githooks/pre-push"
$Failures = New-Object System.Collections.Generic.List[string]

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        $script:Failures.Add($Message)
    }
}

Assert-True (Test-Path -LiteralPath $WorkflowPath) "Missing .github/workflows/ci.yml"

if (Test-Path -LiteralPath $WorkflowPath) {
    $Workflow = Get-Content -LiteralPath $WorkflowPath -Raw
    Assert-True ($Workflow -notmatch '(?m)^\s*path:\s*build\s*$') "windows-msvc must not cache build/ because it stores MSVC PCH files across compiler updates"
    Assert-True ($Workflow -notmatch '(?m)^\s*restore-keys:\s*windows-msvc-\s*$') "windows-msvc must not use broad build cache restore keys"
    Assert-True ($Workflow -match 'tools/pre_push_windows_msvc\.ps1') "windows-msvc should run the same pre_push_windows_msvc.ps1 used by the local hook"
    Assert-True ($Workflow -match 'name:\s*"[^"]*Windows / Demo MSVC"') "windows-msvc should have a descriptive check name"
    Assert-True ($Workflow -match 'static-checks:') "CI should include a static-checks job"
    Assert-True ($Workflow -match 'name:\s*"[^"]*Static checks / CI guardrails"') "static-checks should have a descriptive check name"
    Assert-True ($Workflow -match 'tools/test_ci_preflight\.ps1') "static-checks should run test_ci_preflight.ps1"
    Assert-True ($Workflow -match 'windows-msvc-shipping:') "CI should include a Windows shipping demo job"
    Assert-True ($Workflow -match 'name:\s*"[^"]*Windows / Shipping Demo MSVC"') "windows-msvc-shipping should have a descriptive check name"
    Assert-True ($Workflow -match 'pre_push_windows_msvc\.ps1 -Ci -Shipping') "Windows shipping demo job should run the shared preflight in shipping mode"
    Assert-True ($Workflow -match 'name:\s*"[^"]*Linux / Demo GCC 14"') "linux-gcc should have a descriptive check name"
    Assert-True ($Workflow -match 'name:\s*"[^"]*Linux / Demo Clang 18"') "linux-clang should have a descriptive check name"
}

Assert-True (Test-Path -LiteralPath $PreflightPath) "Missing tools/pre_push_windows_msvc.ps1"

if (Test-Path -LiteralPath $PreflightPath) {
    $Preflight = Get-Content -LiteralPath $PreflightPath -Raw
    Assert-True ($Preflight -match 'FATE_FORCE_DEMO_BUILD=ON') "pre_push_windows_msvc.ps1 must force the demo/open-source build locally"
    Assert-True ($Preflight -match 'FATE_SHIPPING=ON') "pre_push_windows_msvc.ps1 must support a shipping demo preflight"
    Assert-True ($Preflight -match 'Assert-PathUnderRepo') "pre_push_windows_msvc.ps1 must guard recursive build directory cleanup"
    Assert-True ($Preflight -match 'cmake --build') "pre_push_windows_msvc.ps1 must run the CMake build, not only configure"
}

Assert-True (Test-Path -LiteralPath $HookPath) "Missing .githooks/pre-push"

if (Test-Path -LiteralPath $HookPath) {
    $Hook = Get-Content -LiteralPath $HookPath -Raw
    Assert-True ($Hook -match 'pre_push_windows_msvc\.ps1') ".githooks/pre-push must invoke pre_push_windows_msvc.ps1"
}

if ($Failures.Count -gt 0) {
    Write-Error ("CI preflight guardrail test failed:`n - " + ($Failures -join "`n - "))
    exit 1
}

Write-Host "CI preflight guardrails OK" -ForegroundColor Green
