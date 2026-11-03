param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$OutputPath,
    [Parameter(Mandatory)][string]$Wix
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path $PackageDirectory).Path
$output = [IO.Path]::GetFullPath($OutputPath)
$work = Join-Path (Split-Path $output) ([IO.Path]::GetFileNameWithoutExtension($output) + '-wix')
New-Item $work -ItemType Directory -Force | Out-Null
$ns = 'http://wixtoolset.org/schemas/v4/wxs'
$xml = [xml]"<Wix xmlns='$ns'/>"
function Add-Element($parent, [string]$name, [hashtable]$attributes) {
    $element = $xml.CreateElement($name, $ns)
    foreach ($key in $attributes.Keys) { $element.SetAttribute($key, [string]$attributes[$key]) }
    [void]$parent.AppendChild($element)
    return $element
}
function Stable-Id([string]$prefix, [string]$path) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($path.ToLowerInvariant())
    return $prefix + [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).Substring(0, 32)
}
function Stable-Guid([string]$path) {
    $bytes = [Text.Encoding]::UTF8.GetBytes('Bokbacken/SerialESPHelper/MSI/' + $path.ToLowerInvariant())
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).Substring(0, 32)
    return ([guid]$hash).ToString()
}
$product = Add-Element $xml.DocumentElement 'Package' @{
    Name = 'Serial ESP Helper'; Manufacturer = 'Bokbacken'; Version = $Version
    UpgradeCode = '67F22744-7F5C-49CB-BA58-0A0AE7DD9DBE'; Scope = 'perUser'
}
[void](Add-Element $product 'MajorUpgrade' @{
    DowngradeErrorMessage = 'A newer version of Serial ESP Helper is already installed.'
    Schedule = 'afterInstallInitialize'
})
[void](Add-Element $product 'MediaTemplate' @{ EmbedCab = 'yes' })
$local = Add-Element $product 'StandardDirectory' @{ Id = 'LocalAppDataFolder' }
$programs = Add-Element $local 'Directory' @{ Id = 'UserPrograms'; Name = 'Programs' }
$install = Add-Element $programs 'Directory' @{ Id = 'INSTALLFOLDER'; Name = 'SerialESPHelper' }
$feature = Add-Element $product 'Feature' @{ Id = 'Main'; Title = 'Serial ESP Helper'; Level = '1' }
$directories = @{ '' = $install }
$files = @(Get-ChildItem $package -File -Recurse | Sort-Object FullName |
    Where-Object { $_.Name -ne 'SerialESPHelper.ini' })
$files += Get-Item (Join-Path $PSScriptRoot 'installed.marker')
foreach ($file in $files) {
    $relative = if ($file.Name -eq 'installed.marker') { 'installed.marker' }
        else { [IO.Path]::GetRelativePath($package, $file.FullName) }
    $directoryPath = [IO.Path]::GetDirectoryName($relative)
    $current = ''
    $parent = $install
    foreach ($part in ($directoryPath -split '[\\/]' | Where-Object { $_ })) {
        $current = if ($current) { "$current\$part" } else { $part }
        if (!$directories.ContainsKey($current)) {
            $directories[$current] = Add-Element $parent 'Directory' @{
                Id = (Stable-Id 'D' $current); Name = $part
            }
        }
        $parent = $directories[$current]
    }
    $id = Stable-Id 'C' $relative
    $component = Add-Element $parent 'Component' @{ Id = $id; Guid = (Stable-Guid $relative) }
    [void](Add-Element $component 'File' @{
        Id = (Stable-Id 'F' $relative); Source = $file.FullName; Name = $file.Name
    })
    [void](Add-Element $component 'RegistryValue' @{
        Root = 'HKCU'; Key = 'Software\Bokbacken\SerialESPHelper\Components'
        Name = $id; Type = 'string'; Value = $Version; KeyPath = 'yes'
    })
    [void](Add-Element $feature 'ComponentRef' @{ Id = $id })
}
$menu = Add-Element $product 'StandardDirectory' @{ Id = 'ProgramMenuFolder' }
$menuFolder = Add-Element $menu 'Directory' @{ Id = 'AppMenuFolder'; Name = 'Serial ESP Helper' }
$shortcut = Add-Element $menuFolder 'Component' @{ Id = 'StartMenuShortcut'; Guid = '*' }
[void](Add-Element $shortcut 'Shortcut' @{
    Id = 'Launch'; Name = 'Serial ESP Helper'; Target = '[INSTALLFOLDER]SerialESPHelper.exe'
    WorkingDirectory = 'INSTALLFOLDER'
})
[void](Add-Element $shortcut 'RegistryValue' @{
    Root = 'HKCU'; Key = 'Software\Bokbacken\SerialESPHelper'; Name = 'StartMenu'
    Type = 'integer'; Value = '1'; KeyPath = 'yes'
})
[void](Add-Element $shortcut 'RemoveFolder' @{ Id = 'RemoveMenu'; On = 'uninstall' })
foreach ($entry in $directories.GetEnumerator()) {
    [void](Add-Element $shortcut 'RemoveFolder' @{
        Id = (Stable-Id 'R' $entry.Key); Directory = $entry.Value.GetAttribute('Id'); On = 'uninstall'
    })
}
[void](Add-Element $feature 'ComponentRef' @{ Id = 'StartMenuShortcut' })
[void](Add-Element $product 'UIRef' @{ Id = 'WixUI_Minimal' })
$license = (Get-Content (Join-Path $PSScriptRoot '../LICENSE') -Raw) + "`n`n" +
    (Get-Content (Join-Path $PSScriptRoot '../NOTICE.txt') -Raw)
$escaped = $license.Replace('\', '\\').Replace('{', '\{').Replace('}', '\}').
    Replace("`r", '').Replace("`n", '\par ')
$rtf = Join-Path $work 'license.rtf'
[IO.File]::WriteAllText($rtf, '{\rtf1\ansi ' + $escaped + '}', [Text.Encoding]::ASCII)
[void](Add-Element $product 'WixVariable' @{ Id = 'WixUILicenseRtf'; Value = $rtf })
$source = Join-Path $work 'installer.wxs'
$xml.Save($source)
& $Wix build $source -arch x64 -ext WixToolset.UI.wixext -o $output
if ($LASTEXITCODE) { throw 'WiX MSI build failed.' }
