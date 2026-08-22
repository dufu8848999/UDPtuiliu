Add-Type -AssemblyName PresentationCore
Add-Type -AssemblyName System.Drawing

$src = "C:\Users\28268\Downloads\avatar.webp"
$pngPath = "D:\Cpp_Udp_vid\Demo\avatar_icon.png"

$image = [System.Windows.Media.Imaging.BitmapDecoder]::Create([System.Uri]::new($src), [System.Windows.Media.Imaging.BitmapCreateOptions]::None, [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
$frame = $image.Frames[0]

$pngEnc = [System.Windows.Media.Imaging.PngBitmapEncoder]::new()
$pngEnc.Frames.Add($frame)
$fs = [System.IO.File]::Create($pngPath)
$pngEnc.Save($fs)
$fs.Close()
Write-Host "PNG saved: $pngPath"

# Now build multi-size ICO from the PNG
$sizes = @(16,32,48,64,128,256)
$baseImg = [System.Drawing.Image]::FromFile($pngPath)
$entryData = @()
$offset = 6 + (16 * $sizes.Count)
foreach($sz in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($baseImg, 0, 0, $sz, $sz)
    $g.Dispose()
    $pngMs = New-Object System.IO.MemoryStream
    $bmp.Save($pngMs, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngBytes = $pngMs.ToArray()
    $pngMs.Dispose()
    $wb = 0
    if ($sz -lt 256) { $wb = $sz }
    $entryData += @{ W=$wb; Data=$pngBytes; Offset=$offset }
    $offset += $pngBytes.Length
}
$baseImg.Dispose()

$fs = [System.IO.File]::Create("D:\Cpp_Udp_vid\Demo\app_icon.ico")
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$sizes.Count)
foreach($e in $entryData) {
    $bw.Write([Byte]$e.W)
    $bw.Write([Byte]$e.W)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$e.Data.Length)
    $bw.Write([UInt32]$e.Offset)
}
foreach($e in $entryData) { $bw.Write($e.Data) }
$bw.Flush()
$bw.Close()
$fs.Close()
Write-Host "ICO created"