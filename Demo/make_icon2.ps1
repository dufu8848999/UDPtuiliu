Add-Type -AssemblyName System.Drawing

$sizes = @(16,32,48,64,128,256)
$baseImg = [System.Drawing.Image]::FromFile("D:\Cpp_Udp_vid\Demo\app_icon.jpg")

# Read the existing icon and extract the PNG data for each size
$icoPath = "D:\Cpp_Udp_vid\Demo\app_icon.ico"

# Build icon entries as old-style BMP (no PNG)
$entries = @()
$imgOffset = 6 + (16 * $sizes.Count)

foreach($sz in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($baseImg, 0, 0, $sz, $sz)
    $g.Dispose()
    
    # Save to PNG in memory
    $pngMs = New-Object System.IO.MemoryStream
    $bmp.Save($pngMs, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngBytes = $pngMs.ToArray()
    $pngMs.Dispose()
    
    $wbmp = 0
    if ($sz -eq 256) { $wbmp = 0 } else { $wbmp = $sz }
    
    $entries += @{
        Width = $wbmp
        Height = $wbmp
        ColorCount = 0
        Planes = 1
        BitCount = 32
        BytesInRes = $pngBytes.Length
        ImageOffset = $imgOffset
        Data = $pngBytes
    }
    $imgOffset += $pngBytes.Length
}
$baseImg.Dispose()

# Write ICO file
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)

# ICONDIR
$bw.Write([UInt16]0)      # reserved
$bw.Write([UInt16]1)      # type = 1 (icon)
$bw.Write([UInt16]$sizes.Count)

# ICONDIRENTRY for each size
foreach($e in $entries) {
    $bw.Write([Byte]$e.Width)
    $bw.Write([Byte]$e.Height)
    $bw.Write([Byte]$e.ColorCount)
    $bw.Write([Byte]0)      # reserved
    $bw.Write([UInt16]$e.Planes)
    $bw.Write([UInt16]$e.BitCount)
    $bw.Write([UInt32]$e.BytesInRes)
    $bw.Write([UInt32]$e.ImageOffset)
}

# Image data
foreach($e in $entries) {
    $bw.Write($e.Data)
}

$bw.Flush()
$bw.Close()
$fs.Close()

Write-Host "ICO with PNG data created"