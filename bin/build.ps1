#!/usr/bin/env pwsh
# Build the palettegen gem: install dependencies, compile the native C++
# extension, and package the .gem into pkg/.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

bundle install
bundle exec rake compile   # build the native extension (ext/palettegen)
bundle exec rake build     # package pkg/palettegen-<version>.gem

$gem = Get-ChildItem pkg/*.gem | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Host "`nBuilt: $($gem.FullName)"
