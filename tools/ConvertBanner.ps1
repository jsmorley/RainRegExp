#requires -Version 7.0
# Copyright (c) 2026 Jeffrey Morley. CC BY-NC-SA 3.0.
param(
    [string]$Source = (Join-Path $PSScriptRoot '..\src\RainRegExp.bmp'),
    [string]$IconSource = (Join-Path $PSScriptRoot '..\src\Rainmeter.ico'),
    [string]$Destination = (Join-Path $PSScriptRoot '..\src\RainRegExp.png')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$original = [Drawing.Bitmap]::new([IO.Path]::GetFullPath($Source))
$result = [Drawing.Bitmap]::new($original.Width,$original.Height,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
try {
    # Original solid ink colors: powder blue A1BADC and gray B3B3B3.
    # Recover coverage from the nearest ink-to-white blend, then remove the
    # white matte from the RGB channels. Simply deleting pure white leaves
    # opaque, near-white antialias pixels that show up as a halo in dark mode.
    $inks = @(@(161,186,220), @(179,179,179))
    for ($y = 0; $y -lt $original.Height; ++$y) {
        for ($x = 0; $x -lt $original.Width; ++$x) {
            $pixel = $original.GetPixel($x,$y)
            $rgb = @([double]$pixel.R,[double]$pixel.G,[double]$pixel.B)
            $coverage = 0.0
            $bestError = [double]::PositiveInfinity
            foreach ($ink in $inks) {
                $dot = 0.0; $length = 0.0
                for ($channel = 0; $channel -lt 3; ++$channel) {
                    $delta = 255.0 - $ink[$channel]
                    $dot += $delta * (255.0 - $rgb[$channel])
                    $length += $delta * $delta
                }
                $alpha = [Math]::Clamp($dot / $length,0.0,1.0)
                $error = 0.0
                for ($channel = 0; $channel -lt 3; ++$channel) {
                    $difference = $rgb[$channel] - (255.0 + $alpha * ($ink[$channel] - 255.0))
                    $error += $difference * $difference
                }
                if ($error -lt $bestError) { $bestError = $error; $coverage = $alpha }
            }
            $opacity = [int][Math]::Round(255.0 * $coverage)
            if ($opacity -eq 0) { $result.SetPixel($x,$y,[Drawing.Color]::Transparent); continue }
            $alpha = $opacity / 255.0
            $foreground = foreach ($value in $rgb) {
                [int][Math]::Clamp([Math]::Round(($value - 255.0 * (1.0 - $alpha)) / $alpha),0.0,255.0)
            }
            $result.SetPixel($x,$y,[Drawing.Color]::FromArgb($opacity,$foreground[0],$foreground[1],$foreground[2]))
        }
    }
    # The original bitmap cuts through the droplet's bottom row. Use the
    # supplied complete icon instead, retaining the original un-matted text.
    # Extract the embedded PNG directly; Icon.ToBitmap can select a smaller frame.
    $bytes = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($IconSource))
    $count = [BitConverter]::ToUInt16($bytes,4)
    $entry = 6 + 16 * ($count - 1)
    $length = [BitConverter]::ToUInt32($bytes,$entry + 8)
    $offset = [BitConverter]::ToUInt32($bytes,$entry + 12)
    if ($bytes[$offset] -ne 137 -or $bytes[$offset + 1] -ne 80) { throw 'Expected the largest ICO frame to be PNG.' }
    $stream = [IO.MemoryStream]::new($bytes,$offset,$length)
    $drop = [Drawing.Bitmap]::new($stream)
    $canvas = $null; $graphics = $null; $attributes = $null
    try {
        $left = $drop.Width; $top = $drop.Height; $right = -1; $bottom = -1
        for ($y = 0; $y -lt $drop.Height; ++$y) {
            for ($x = 0; $x -lt $drop.Width; ++$x) {
                if ($drop.GetPixel($x,$y).A -gt 0) {
                    $left = [Math]::Min($left,$x); $top = [Math]::Min($top,$y)
                    $right = [Math]::Max($right,$x); $bottom = [Math]::Max($bottom,$y)
                }
            }
        }
        $dropWidth = [int][Math]::Round($original.Height * ($right - $left + 1.0) / ($bottom - $top + 1.0))
        $textX = 3 + $dropWidth + 7
        $textWidth = $original.Width - 40
        $canvas = [Drawing.Bitmap]::new($textX + $textWidth + 3,$original.Height + 6,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($canvas)
        $graphics.Clear([Drawing.Color]::Transparent)
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
        $attributes = [Drawing.Imaging.ImageAttributes]::new()
        $attributes.SetWrapMode([Drawing.Drawing2D.WrapMode]::TileFlipXY)
        $graphics.DrawImage($drop,[Drawing.Rectangle]::new(3,3,$dropWidth,$original.Height),$left,$top,$right-$left+1,$bottom-$top+1,[Drawing.GraphicsUnit]::Pixel,$attributes)
        $graphics.DrawImage($result,[Drawing.Rectangle]::new($textX,3,$textWidth,$original.Height),40,0,$textWidth,$original.Height,[Drawing.GraphicsUnit]::Pixel,$attributes)
        $canvas.Save([IO.Path]::GetFullPath($Destination),[Drawing.Imaging.ImageFormat]::Png)
    } finally {
        if ($attributes) { $attributes.Dispose() }
        if ($graphics) { $graphics.Dispose() }
        if ($canvas) { $canvas.Dispose() }
        $drop.Dispose(); $stream.Dispose()
    }
} finally {
    $result.Dispose()
    $original.Dispose()
}
