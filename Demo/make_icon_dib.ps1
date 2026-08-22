Add-Type -AssemblyName System.Drawing

$src = "D:\Cpp_Udp_vid\Demo\avatar_icon.png"
$icoPath = "D:\Cpp_Udp_vid\Demo\app_icon_dib.ico"
$sizes = @(16,32,48,64,128,256)
$srcImg = [System.Drawing.Image]::FromFile($src)

$entries = @()
$offset = 6 + (16 * $sizes.Count)

foreach($sz in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($srcImg, 0, 0, $sz, $sz)
    $g.Dispose()

    $rect = [System.Drawing.Rectangle]::new(0,0,$sz,$sz)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $pixelCount = $sz * $sz
    $xor = New-Object byte[] ($sz * $sz * 4)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $xor, 0, $xor.Length)
    $bmp.UnlockBits($data)
    $bmp.Dispose()

    # AND mask: 1 bit per pixel, bottom-up rows, each row padded to 32-bit
    $andRowBytes = ([Math]::Ceiling($sz / 8.0))
    $andPadded = ([Math]::Ceiling($andRowBytes / 4.0)) * 4
    $and = New-Object byte[] ($andPadded * $sz)  # all 0 => use pixel

    # Build DIB = BITMAPINFOHEADER + XOR + AND
    $ms = [System.IO.MemoryStream]::new()
    $bw = [System.IO.BinaryWriter]::new($ms)
    $bw.Write([UInt32]40)                       # biSize
    $bw.Write([Int32]$sz)                       # biWidth
    $bw.Write([Int32]($sz * 2))                 # biHeight = 2x (XOR + AND)
    $bw.Write([UInt16]1)                        # biPlanes
    $bw.Write([UInt16]32)                       # biBitCount
    $bw.Write([UInt32]0)                        # biCompression = BI_RGB
    $bw.Write([UInt32]($xor.Length + $and.Length)) # biSizeImage
    $bw.Write([Int32]0); $bw.Write([Int32]0); $bw.Write([UInt32]0); $bw.Write([UInt32]0) # biXPels/biYPels/biClrUsed/biClrImportant
    $bw.Write($xor)
    $bw.Write($and)
    $bw.Flush()
    $dib = $ms.ToArray()
    $bw.Dispose(); $ms.Dispose()

    $wEntry = 0
    if ($sz -lt 256) { $wEntry = $sz }
    $entries += @{ W=$wEntry; H=$wEntry; DIB=$dib; Offset=$offset }
    $offset += $dib.Length
}
$srcImg.Dispose()

$fs = [System.IO.File]::Create($icoPath)
$bw = [System.IO.BinaryWriter]::new($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
foreach($e in $entries) {
    $bw.Write([Byte]$e.W)
    $bw.Write([Byte]$e.H)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)                 # planes
    $bw.Write([UInt16]32)                # bitcount
    $bw.Write([UInt32]$e.DIB.Length)     # bytes in res
    $bw.Write([UInt32]$e.Offset)
}
foreach($e in $entries) { $bw.Write($e.DIB) }
$bw.Flush(); $bw.Dispose(); $fs.Dispose()
Write-Host "DIB ICO created with $($sizes.Count) sizes"