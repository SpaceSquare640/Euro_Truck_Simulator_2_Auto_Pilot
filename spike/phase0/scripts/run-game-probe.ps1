# Edited by: OpenAI-Codex_Responsible: Source_Code
param(
    [ValidateSet('generic', 'semantical')][string]$Device = 'semantical',
    [string]$OutputDirectory,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

function Read-CsvTail([string]$Path, [int]$Count = 20000) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing CSV: $Path" }
    $header = Get-Content -LiteralPath $Path -TotalCount 1
    if ([string]::IsNullOrWhiteSpace($header)) { throw "Empty CSV: $Path" }
    $lines = @(Get-Content -LiteralPath $Path -Tail $Count | Where-Object {
        $_ -and $_ -ne $header
    })
    return @($lines | ConvertFrom-Csv -Header ($header -split ','))
}

function Get-Session([string]$Directory) {
    $files = @(Get-ChildItem -LiteralPath $Directory -Filter 'frames_*.csv' -File |
        Sort-Object LastWriteTimeUtc -Descending)
    foreach ($frame in $files) {
        $inputName = $frame.Name -replace '^frames_', 'inputs_'
        $inputPath = Join-Path $Directory $inputName
        if (Test-Path -LiteralPath $inputPath -PathType Leaf) {
            return [pscustomobject]@{ Frame = $frame.FullName; Input = $inputPath }
        }
    }
    throw 'No matching frame/input CSV session found.'
}

function Resolve-OutputDirectory([string]$Override) {
    if ([string]::IsNullOrWhiteSpace($Override)) {
        $localAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
        if ([string]::IsNullOrWhiteSpace($localAppData)) { $localAppData = $env:LOCALAPPDATA }
        if ([string]::IsNullOrWhiteSpace($localAppData)) { throw 'Could not resolve LocalAppData.' }
        return Join-Path $localAppData 'ETS2AP_Spike'
    }
    if (-not [System.IO.Path]::IsPathRooted($Override)) {
        throw 'OutputDirectory must be an absolute path.'
    }
    return [System.IO.Path]::GetFullPath($Override)
}

function Assert-SafeFrame($Row, [bool]$CheckManualSteering) {
    $speed = if ([string]::IsNullOrWhiteSpace($Row.speed)) { [double]::NaN } else {
        [double]::Parse($Row.speed, [cultureinfo]::InvariantCulture)
    }
    $throttle = if ([string]::IsNullOrWhiteSpace($Row.thr_eff)) { [double]::NaN } else {
        [double]::Parse($Row.thr_eff, [cultureinfo]::InvariantCulture)
    }
    if ($Row.paused -ne '0' -or $Row.parking_brake -ne '1' -or
        [double]::IsNaN($speed) -or [double]::IsInfinity($speed) -or
        [Math]::Abs($speed) -ge 0.1 -or
        [double]::IsNaN($throttle) -or [double]::IsInfinity($throttle) -or
        [Math]::Abs($throttle) -ge 0.01) {
        throw 'Truck must be unpaused, stopped, with parking brake applied and no effective throttle.'
    }
    if ($CheckManualSteering) {
        $steering = if ([string]::IsNullOrWhiteSpace($Row.steer_in)) { [double]::NaN } else {
            [double]::Parse($Row.steer_in, [cultureinfo]::InvariantCulture)
        }
        if ([double]::IsNaN($steering) -or [double]::IsInfinity($steering) -or
            [Math]::Abs($steering) -ge 0.05) {
            throw 'Steering input must be neutral before the probe.'
        }
    }
}

function Assert-Parked($Frames) {
    $recent = @($Frames | Select-Object -Last 10)
    if ($recent.Count -lt 10) { throw 'Too few telemetry frames.' }
    foreach ($row in $recent) { Assert-SafeFrame $row $true }
}

function Test-ProbeResult($Frames, $Inputs, [string]$DeviceName, [long]$StartFrameNs, [long]$StartInputNs) {
    $freshInputs = @($Inputs | Where-Object {
        $_.device -eq $DeviceName -and [long]$_.qpc_ns -gt $StartInputNs -and
        $_.mode -eq 'steer' -and $_.active -eq '1' -and
        $_.steering_event_reported -eq '1'
    })
    $positive = @($freshInputs | Where-Object {
        [double]::Parse($_.steering, [cultureinfo]::InvariantCulture) -ge 0.4
    })
    $negative = @($freshInputs | Where-Object {
        [double]::Parse($_.steering, [cultureinfo]::InvariantCulture) -le -0.4
    })
    if ($positive.Count -lt 10 -or $negative.Count -lt 10) {
        return [pscustomobject]@{ Passed = $false; Reason = 'No complete positive and negative input pulse'; Positive = $positive.Count; Negative = $negative.Count }
    }

    $freshFrames = @($Frames | Where-Object {
        [long]$_.qpc_ns -gt $StartFrameNs -and $_.paused -eq '0' -and
        -not [string]::IsNullOrWhiteSpace($_.steer_eff)
    })
    if ($freshFrames.Count -lt 20) {
        return [pscustomobject]@{ Passed = $false; Reason = 'No fresh effective steering telemetry'; Positive = $positive.Count; Negative = $negative.Count }
    }
    $plusMapped = @($freshFrames | Where-Object {
        $t = ([long]$_.qpc_ns - [long]$positive[0].qpc_ns) / 1e9
        $t -ge 0.2 -and $t -le 1.2 -and -not [string]::IsNullOrWhiteSpace($_.steer_in)
    })
    $minusMapped = @($freshFrames | Where-Object {
        $t = ([long]$_.qpc_ns - [long]$negative[0].qpc_ns) / 1e9
        $t -ge 0.2 -and $t -le 1.2 -and -not [string]::IsNullOrWhiteSpace($_.steer_in)
    })
    if ($plusMapped.Count -lt 5 -or $minusMapped.Count -lt 5) {
        return [pscustomobject]@{ Passed = $false; Reason = 'Mapped steering telemetry missing'; Positive = $positive.Count; Negative = $negative.Count }
    }
    $plusInput = ($plusMapped | Measure-Object -Property steer_in -Average).Average
    $minusInput = ($minusMapped | Measure-Object -Property steer_in -Average).Average
    if ($plusInput -le -0.5 -and $minusInput -ge 0.5) {
        return [pscustomobject]@{
            Passed = $false
            Reason = 'Game steering axis is inverted relative to Spike steering values'
            Positive = $positive.Count
            Negative = $negative.Count
        }
    }
    if ($plusInput -lt 0.5 -or $minusInput -gt -0.5) {
        return [pscustomobject]@{ Passed = $false; Reason = 'Game input steering did not follow both logical commands'; Positive = $positive.Count; Negative = $negative.Count }
    }
    $baseline = @($freshFrames | Where-Object {
        $t = ([long]$positive[0].qpc_ns - [long]$_.qpc_ns) / 1e9
        $t -ge 0.2 -and $t -le 1.0
    })
    $plus = @($freshFrames | Where-Object {
        $t = ([long]$_.qpc_ns - [long]$positive[0].qpc_ns) / 1e9
        $t -ge 0.2 -and $t -le 2.5
    })
    $beforeMinus = @($freshFrames | Where-Object {
        $t = ([long]$negative[0].qpc_ns - [long]$_.qpc_ns) / 1e9
        $t -ge 0.2 -and $t -le 1.0
    })
    $minus = @($freshFrames | Where-Object {
        $t = ([long]$_.qpc_ns - [long]$negative[0].qpc_ns) / 1e9
        $t -ge 0.2 -and $t -le 2.5
    })
    if ($baseline.Count -lt 5 -or $plus.Count -lt 5 -or
        $beforeMinus.Count -lt 5 -or $minus.Count -lt 5) {
        return [pscustomobject]@{ Passed = $false; Reason = 'Baseline or pulse telemetry missing'; Positive = $positive.Count; Negative = $negative.Count }
    }
    $baseMean = ($baseline | Measure-Object -Property steer_eff -Average).Average
    $plusMean = ($plus | Measure-Object -Property steer_eff -Average).Average
    $beforeMinusMean = ($beforeMinus | Measure-Object -Property steer_eff -Average).Average
    $minusMean = ($minus | Measure-Object -Property steer_eff -Average).Average
    $plusDelta = $plusMean - $baseMean
    $minusDelta = $minusMean - $beforeMinusMean
    $passed = [Math]::Abs($baseMean) -lt 0.05 -and $plusDelta -ge 0.05 -and $minusDelta -le -0.05
    return [pscustomobject]@{
        Passed = $passed
        Reason = if ($passed) { 'Effective steering followed both signed input pulses' } else { 'Effective steering did not follow the signed pulses from neutral' }
        Positive = $positive.Count
        Negative = $negative.Count
        EffectiveDelta = [Math]::Round($plusDelta - $minusDelta, 4)
    }
}

function Invoke-SelfTest {
    $overridePath = Join-Path ([System.IO.Path]::GetTempPath()) 'ets2ap-spike-self-test'
    $resolvedPath = Resolve-OutputDirectory $overridePath
    if ($resolvedPath -ne [System.IO.Path]::GetFullPath($overridePath)) {
        throw 'Self-test failed: explicit output directory was not preserved.'
    }
    $frame = [pscustomobject]@{ qpc_ns = '1000000000'; paused = '0'; speed = '0'; parking_brake = '1'; gear_displayed = '4'; thr_eff = '0'; brk_eff = '1'; steer_in = '0'; steer_eff = '0' }
    Assert-Parked (@($frame) * 10)
    $invalid = $frame.PSObject.Copy(); $invalid.parking_brake = '0'
    $rejected = $false
    try { Assert-Parked (@($invalid) * 10) } catch { $rejected = $true }
    if (-not $rejected) { throw 'Self-test failed: released parking brake was accepted.' }
    $throttling = $frame.PSObject.Copy(); $throttling.thr_eff = '0.2'
    $rejected = $false
    try { Assert-SafeFrame $throttling $false } catch { $rejected = $true }
    if (-not $rejected) { throw 'Self-test failed: effective throttle was accepted.' }
    $moving = $frame.PSObject.Copy(); $moving.speed = '0.3'
    $rejected = $false
    try { Assert-SafeFrame $moving $false } catch { $rejected = $true }
    if (-not $rejected) { throw 'Self-test failed: movement during the probe was accepted.' }
    $nanSpeed = $frame.PSObject.Copy(); $nanSpeed.speed = 'NaN'
    $rejected = $false
    try { Assert-SafeFrame $nanSpeed $false } catch { $rejected = $true }
    if (-not $rejected) { throw 'Self-test failed: NaN speed was accepted.' }
    $inputs = @()
    $frames = @()
    foreach ($i in 0..19) {
        $inputs += [pscustomobject]@{ device = 'ets2ap_spike'; qpc_ns = [string](2000000000L + $i * 10000000L); mode = 'steer'; active = '1'; events_reported = '4'; steering_event_reported = '1'; steering = '0.5' }
        $inputs += [pscustomobject]@{ device = 'ets2ap_spike'; qpc_ns = [string](7000000000L + $i * 10000000L); mode = 'steer'; active = '1'; events_reported = '4'; steering_event_reported = '1'; steering = '-0.5' }
        $frames += [pscustomobject]@{ qpc_ns = [string](1300000000L + $i * 10000000L); paused = '0'; steer_in = '0'; steer_eff = '0' }
        $frames += [pscustomobject]@{ qpc_ns = [string](2400000000L + $i * 10000000L); paused = '0'; steer_in = '0.8'; steer_eff = '0.4' }
        $frames += [pscustomobject]@{ qpc_ns = [string](6300000000L + $i * 10000000L); paused = '0'; steer_in = '0'; steer_eff = '0.4' }
        $frames += [pscustomobject]@{ qpc_ns = [string](7400000000L + $i * 10000000L); paused = '0'; steer_in = '-0.8'; steer_eff = '-0.4' }
    }
    $result = Test-ProbeResult $frames $inputs 'ets2ap_spike' 1000000000L 1000000000L
    if (-not $result.Passed) { throw "Self-test failed: $($result.Reason)" }
    $missing = Test-ProbeResult $frames @($inputs | Where-Object steering -ne '-0.5') 'ets2ap_spike' 1000000000L 1000000000L
    if ($missing.Passed) { throw 'Self-test failed: missing negative pulse was accepted.' }
    $inverted = @($frames | ForEach-Object {
        $copy = $_.PSObject.Copy()
        $copy.steer_in = [string](-[double]$_.steer_in)
        $copy.steer_eff = [string](-[double]$_.steer_eff)
        $copy
    })
    $invertedResult = Test-ProbeResult $inverted $inputs 'ets2ap_spike' 1000000000L 1000000000L
    if ($invertedResult.Passed -or $invertedResult.Reason -notlike '*inverted*') {
        throw 'Self-test failed: inverted steering response was accepted.'
    }
    $noEvent = @($inputs | ForEach-Object {
        $copy = $_.PSObject.Copy(); $copy.steering_event_reported = '0'; $copy
    })
    if ((Test-ProbeResult $frames $noEvent 'ets2ap_spike' 1000000000L 1000000000L).Passed) {
        throw 'Self-test failed: missing steering event was accepted.'
    }
    'PASS: parked safety, released parking brake/throttle/speed, directional response, missing pulse/event'
}

if ($SelfTest) { Invoke-SelfTest; exit 0 }

$directory = Resolve-OutputDirectory $OutputDirectory
$modePath = Join-Path $directory 'mode.txt'
$mustNeutralize = $false
$neutralAckRequired = $false
$session = $null
$failed = $false
$deviceName = if ($Device -eq 'semantical') { 'ets2ap_spike_sem' } else { 'ets2ap_spike' }
$successLine = ''
try {
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) { throw 'Spike output directory does not exist.' }
    $mustNeutralize = $true
    $session = Get-Session $directory
    $configPath = Join-Path $directory 'config.txt'
    if (Test-Path -LiteralPath $configPath -PathType Leaf) {
        foreach ($line in @(Get-Content -LiteralPath $configPath)) {
            if ($line -match '^\s*script_speed\s*=\s*(\S+)') {
                if ([double]::Parse($Matches[1], [cultureinfo]::InvariantCulture) -ne 1.0) {
                    throw 'Probe requires script_speed=1.'
                }
            }
        }
    }
    if (Test-Path -LiteralPath $modePath -PathType Leaf) {
        $priorMode = ((Get-Content -LiteralPath $modePath -Raw -ErrorAction Stop).Trim() -split '\s+')[0]
        if ($priorMode -ne 'off') {
            $neutralAckRequired = $true
            throw 'Spike mode was not off; it will be neutralized without starting a probe.'
        }
    }
    if ((Get-Item -LiteralPath $session.Frame).LastWriteTimeUtc -lt [datetime]::UtcNow.AddSeconds(-5)) {
        throw 'Spike telemetry CSV is stale; verify that the plugin is loaded.'
    }
    $baselineFrames = Read-CsvTail $session.Frame
    Assert-Parked $baselineFrames
    $beforeCountdownNs = [long]$baselineFrames[-1].qpc_ns
    Write-Host 'Focus the ETS2 driving view now. The steering probe starts in 10 seconds.'
    Start-Sleep -Seconds 10
    $currentFrames = Read-CsvTail $session.Frame
    Assert-Parked $currentFrames
    if ([long]$currentFrames[-1].qpc_ns -le $beforeCountdownNs) { throw 'Telemetry did not advance during countdown.' }
    $currentInputs = @(Read-CsvTail $session.Input | Where-Object { $_.device -eq $deviceName })
    if ($currentInputs.Count -eq 0 -or [long]$currentInputs[-1].qpc_ns -le $beforeCountdownNs -or
        $currentInputs[-1].active -ne '1' -or
        -not $currentInputs[-1].PSObject.Properties['steering_event_reported']) {
        throw "Device $deviceName did not become active during countdown."
    }
    $startFrameNs = [long]$currentFrames[-1].qpc_ns
    $startInputNs = [long]$currentInputs[-1].qpc_ns
    $lastFrameNumber = [long]$currentFrames[-1].frame
    $lastInputFrame = [long]$currentInputs[-1].input_frame
    $lastFrameNs = $startFrameNs
    $lastInputNs = $startInputNs
    $lastFrameAdvanceAt = [datetime]::UtcNow
    $capturedFrames = [System.Collections.Generic.List[object]]::new()
    $capturedInputs = [System.Collections.Generic.List[object]]::new()
    Set-Content -LiteralPath $modePath -Value "steer $([guid]::NewGuid().ToString('N'))" -Encoding Ascii
    $neutralAckRequired = $true
    $deadline = [datetime]::UtcNow.AddSeconds(11)
    while ([datetime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
        foreach ($row in @(Read-CsvTail $session.Frame)) {
            $qpc = [long]$row.qpc_ns
            if ($qpc -le $lastFrameNs) { continue }
            if ([long]$row.frame -ne $lastFrameNumber + 1) { throw 'Telemetry CSV frame gap during probe.' }
            Assert-SafeFrame $row $false
            $capturedFrames.Add($row)
            $lastFrameNs = $qpc
            $lastFrameNumber = [long]$row.frame
            $lastFrameAdvanceAt = [datetime]::UtcNow
        }
        foreach ($row in @(Read-CsvTail $session.Input)) {
            if ($row.device -ne $deviceName) { continue }
            $qpc = [long]$row.qpc_ns
            if ($qpc -le $lastInputNs) { continue }
            if ([long]$row.input_frame -ne $lastInputFrame + 1) { throw 'Input CSV frame gap during probe.' }
            $capturedInputs.Add($row)
            $lastInputNs = $qpc
            $lastInputFrame = [long]$row.input_frame
        }
        if ([datetime]::UtcNow -gt $lastFrameAdvanceAt.AddSeconds(3)) {
            throw 'Telemetry stopped advancing during probe.'
        }
    }
    $result = Test-ProbeResult $capturedFrames $capturedInputs $deviceName $startFrameNs $startInputNs
    if (-not $result.Passed) { throw "FAIL: $($result.Reason); positive=$($result.Positive), negative=$($result.Negative)" }
    $successLine = "PASS: $($result.Reason); effective delta=$($result.EffectiveDelta); positive=$($result.Positive), negative=$($result.Negative)"
} catch {
    Write-Host "FAIL: $($_.Exception.Message)"
    $failed = $true
} finally {
    if ($mustNeutralize) {
        $beforeOffNs = 0L
        $offBaselineKnown = $false
        try {
            if ($session) {
                $beforeOffRows = @(Read-CsvTail $session.Input | Where-Object { $_.device -eq $deviceName })
                if ($beforeOffRows.Count) {
                    $beforeOffNs = [long]$beforeOffRows[-1].qpc_ns
                    $offBaselineKnown = $true
                }
            }
        } catch {
            Write-Host "WARN: Could not read the input baseline before off: $($_.Exception.Message)"
        }
        try {
            Set-Content -LiteralPath $modePath -Value "off $([guid]::NewGuid().ToString('N'))" -Encoding Ascii
            $neutralConfirmed = $false
            if ($neutralAckRequired -and $session -and $offBaselineKnown) {
                $offDeadline = [datetime]::UtcNow.AddSeconds(4)
                while ([datetime]::UtcNow -lt $offDeadline) {
                    Start-Sleep -Milliseconds 250
                    $offRows = @(Read-CsvTail $session.Input | Where-Object {
                        $_.device -eq $deviceName -and [long]$_.qpc_ns -gt $beforeOffNs -and
                        $_.mode -eq 'off' -and $_.steering -eq '0' -and
                        $_.steering_event_reported -eq '1'
                    })
                    if ($offRows.Count) { $neutralConfirmed = $true; break }
                }
            }
            if ($neutralAckRequired) {
                if (-not $neutralConfirmed) { throw 'off was written but a neutral input frame was not acknowledged.' }
                Start-Sleep -Seconds 1
                foreach ($row in @(Read-CsvTail $session.Frame)) {
                    $qpc = [long]$row.qpc_ns
                    if ($qpc -le $lastFrameNs -or $qpc -gt [long]$offRows[-1].qpc_ns) { continue }
                    if ([long]$row.frame -ne $lastFrameNumber + 1) { throw 'Telemetry CSV frame gap before off acknowledgement.' }
                    Assert-SafeFrame $row $false
                    $lastFrameNs = $qpc
                    $lastFrameNumber = [long]$row.frame
                }
                Write-Host 'Spike mode: off, neutral frame confirmed'
            } else {
                Write-Host 'Spike mode: off, no probe command was issued'
            }
        } catch {
            Write-Host "FAIL: Unable to confirm Spike neutralization: $($_.Exception.Message)"
            $failed = $true
        }
    }
}
if ($failed) { exit 1 }
if ($successLine) { Write-Host $successLine }
