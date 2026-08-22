Add-Type -AssemblyName System.Drawing

$src = "D:\Cpp_Udp_vid\Demo\avatar_icon.png"
$icoPath = "D:\Cpp_Udp_vid\Demo\app_icon_bmp.ico"

$size = 32
$img = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($img)
$g.Clear([System.Drawing.Color]::Transparent)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$srcImg = [System.Drawing.Image]::FromFile($src)
$g.DrawImage($srcImg, 0, 0, $size, $size)
$g.Dispose()
$srcImg.Dispose()

# Extract raw BGRA pixels
$rect = [System.Drawing.Rectangle]::new(0,0,$size,$size)
$data = $img.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$pixelBytes = [Math]::Abs($stride) * $size
$xor = New-Object byte[] $pixelBytes
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $xor, 0, $pixelBytes)
$img.UnlockBits($data)
$img.Dispose()

# AND mask: all transparent (0 = use, 1 = transparent/not drawn). one bit per pixel, padded to 32-bit rows
$andStride = [Math]::Ceiling($size / 8.0)
$andPadded = [Math]::Ceiling($andStride / 4.0) * 4
$and = New-Object byte[] ($andPadded * $size)

# ICONDIR (6) + ICONDIRENTRY (16) = 22
$offset = 22
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)

$bw.Write([UInt16]0)          # reserved
$bw.Write([UInt16]1)          # type icon
$bw.Write([UInt16]1)          # count

# ICONDIRENTRY
$bw.Write([Byte]$size)        # width
$bw.Write([Byte]$size)        # height
$bw.Write([Byte]0)            # colors
$bw.Write([Byte]0)            # reserved
$bw.Write([UInt16]1)          # planes
$bw.Write([UInt16]32)         # bitcount
$xorLen = $xor.Length
$andLen = $and.Length
$bw.Write([UInt32]($xorLen + $andLen))  # bytes in res
$bw.Write([UInt32]$offset)    # offset

# XOR data (raw BGRA, top-down)
$bw.Write($xor)
# AND mask
$bw.Write($and)

$bw.Flush()
$bw.Close()
$fs.Close()
Write-Host "BMP ICO created at $icoPath"