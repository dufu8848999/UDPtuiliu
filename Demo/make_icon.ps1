Add-Type -AssemblyName System.Drawing

$sizes = @(16,32,48,64,128,256)
$baseImg = [System.Drawing.Image]::FromFile("D:\Cpp_Udp_vid\Demo\app_icon.jpg")
$entryData = @()
$offset = 6 + (16 * $sizes.Count)

foreach($sz in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($baseImg, 0, 0, $sz, $sz)
    $g.Dispose()
    $rect = New-Object System.Drawing.Rectangle(0,0,$sz,$sz)
    $bmpData = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $bmpData.Stride
    $bytes = [Math]::Abs($stride) * $sz
    $data = New-Object byte[] $bytes
    [System.Runtime.InteropServices.Marshal]::Copy($bmpData.Scan0, $data, 0, $bytes)
    $bmp.UnlockBits($bmpData)
    $bmp.Dispose()
    $entryData += @{ Size=$sz; Data=$data; Offset=$offset }
    $offset += $bytes
}
$baseImg.Dispose()

$fs = [System.IO.File]::Create("D:\Cpp_Udp_vid\Demo\app_icon.ico")
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$sizes.Count)

foreach($e in $entryData) {
    $sz = $e.Size
    if ($sz -ge 256) { $sb = 0 } else { $sb = $sz }
    $bw.Write([Byte]$sb)
    $bw.Write([Byte]$sb)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$e.Data.Length)
    $bw.Write([UInt32]$e.Offset)
}

foreach($e in $entryData) {
    $bw.Write($e.Data)
}

$bw.Flush()
$bw.Close()
$fs.Close()
Write-Host "ICO created successfully"