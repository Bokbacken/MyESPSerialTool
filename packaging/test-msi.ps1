param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][string]$UpgradeInstaller,
    [Parameter(Mandatory)][string]$LogDirectory
)
$ErrorActionPreference = 'Stop'
New-Item $LogDirectory -ItemType Directory -Force | Out-Null
$root = Join-Path $env:LOCALAPPDATA 'Programs/SerialESPHelper'
if (Test-Path $root) { throw "Refusing installer test over existing user data: $root" }
$logs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'SerialESPHelper/Logs'
$sentinel = Join-Path $logs 'msi-preservation-test.log'
if (Test-Path $sentinel) { throw 'Refusing to overwrite existing test sentinel.' }
$engine = New-Object -ComObject WindowsInstaller.Installer
$database = $engine.OpenDatabase([IO.Path]::GetFullPath($Installer), 0)
$query = $database.OpenView('SELECT `Value` FROM `Property` WHERE `Property` = ''ALLUSERS''')
$query.Execute()
$record = $query.Fetch()
if ($record -and $record.StringData(1)) { throw 'MSI must not set ALLUSERS (per-machine installation).' }
if (($database.SummaryInformation(0).Property(15) -band 8) -eq 0) {
    throw 'MSI requests elevated privileges.'
}
$query.Close()
function Invoke-Msi([string]$action, [string]$path, [string]$name) {
    $absolute = [IO.Path]::GetFullPath($path)
    $log = Join-Path ([IO.Path]::GetFullPath($LogDirectory)) "$name.log"
    $process = Start-Process msiexec.exe -ArgumentList "$action `"$absolute`" /qn /norestart /l*v `"$log`"" -PassThru
    if (!$process.WaitForExit(180000)) {
        $process.Kill()
        throw "MSI $name timed out."
    }
    if ($process.ExitCode -notin @(0, 3010)) { throw "MSI $name failed: $($process.ExitCode). See $log" }
}
Invoke-Msi '/i' $Installer 'install'
$app = Join-Path $root 'SerialESPHelper.exe'
$ini = Join-Path $root 'SerialESPHelper.ini'
if (!(Test-Path $app) -or !(Test-Path (Join-Path $root 'installed.marker'))) {
    throw 'MSI did not install to the per-user LocalAppData directory.'
}
if (Test-Path $ini) { throw 'MSI must not own or install the user INI.' }
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Serial ESP Helper/Serial ESP Helper.lnk'
if (!(Test-Path $shortcut)) { throw 'Start menu shortcut missing.' }
$process = Start-Process $app -ArgumentList '--smoke-test' -PassThru
if (!$process.WaitForExit(15000)) { $process.Kill(); throw 'Installed smoke test timed out.' }
if ($process.ExitCode -ne 0) { throw 'Installed app smoke test failed.' }
# First normal launch must create the INI with installed defaults, without connecting.
$process = Start-Process $app -PassThru
try {
    for ($attempt = 0; $attempt -lt 100 -and !(Test-Path $ini); $attempt++) { Start-Sleep -Milliseconds 100 }
    if (!(Test-Path $ini)) { throw 'First launch did not create its user INI.' }
    $content = Get-Content $ini -Raw
    $expected = 'logDirectory=' + $logs.Replace('\', '/')
    if (!$content.Replace('\', '/').Contains($expected)) {
        throw 'Installed log directory does not use the current user Documents folder.'
    }
} finally {
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
$content += "`n; MSI upgrade must retain this custom comment`n"
[IO.File]::WriteAllText($ini, $content, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($ini + '.bak', 'preserved backup')
New-Item $logs -ItemType Directory -Force | Out-Null
[IO.File]::WriteAllText($sentinel, 'preserved log')
Invoke-Msi '/i' $UpgradeInstaller 'upgrade'
if ((Get-Content $ini -Raw) -cne $content) { throw 'Upgrade changed the user INI.' }
if ((Get-Content ($ini + '.bak') -Raw) -ne 'preserved backup') { throw 'Upgrade changed the backup.' }
if ((Get-Content $sentinel -Raw) -ne 'preserved log') { throw 'Upgrade changed user logs.' }
Invoke-Msi '/x' $UpgradeInstaller 'uninstall'
if (Test-Path $app) { throw 'Uninstall did not remove the installed executable.' }
if (Test-Path $shortcut) { throw 'Uninstall did not remove the shortcut.' }
if ((Get-Content $ini -Raw) -cne $content) { throw 'Uninstall removed or changed the user INI.' }
if (!(Test-Path ($ini + '.bak')) -or !(Test-Path $sentinel)) { throw 'Uninstall removed user data.' }
Remove-Item $ini, ($ini + '.bak'), $sentinel
Write-Host 'Per-user install, first-launch defaults, upgrade and uninstall preservation verified.'
