$bytes = [System.IO.File]::ReadAllBytes("D:\Cpp_Udp_vid\Demo\app_icon.ico")
$ms = [System.IO.MemoryStream]::new($bytes)
$br = [System.IO.BinaryReader]::new($ms)
$reserved = $br.ReadUInt16()
$type = $br.ReadUInt16()
$count = $br.ReadUInt16()
Write-Host "reserved=$reserved type=$type count=$count"
for($i=0; $i -lt $count; $i++){
    $w = $br.ReadByte(); $h = $br.ReadByte(); $cc = $br.ReadByte(); $r = $br.ReadByte()
    $planes = $br.ReadUInt16(); $bits = $br.ReadUInt16()
    $len = $br.ReadUInt32(); $off = $br.ReadUInt32()
    Write-Host "  icon#$i dim=${w}x${h} bits=$bits bytesInRes=$len offset=$off"
}
# peek magic of first image
$br.BaseStream.Position = 0
$br.ReadBytes(16) | Out-Null
$firstOff = 0
$ms.Position = 22
$firstOff = [System.IO.BinaryReader]::new($ms).ReadUInt32()
$ms.Position = $firstOff
$magic = $br.ReadUInt32()
$hdr = ($magic -band 0xFF).ToString('X2') + ' ' + (($magic -shr 8) -band 0xFF).ToString('X2')
Write-Host "first image magic: $hdr"