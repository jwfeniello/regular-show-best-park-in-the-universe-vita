$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$assetDir = Join-Path $projectRoot 'extras/livearea'
$masterDir = Join-Path $assetDir 'masters'
$targets = @(
    @{ Input = 'background.png'; Output = 'bg0.png'; Size = '840x500!' },
    @{ Input = 'icon.png'; Output = 'icon0.png'; Size = '128x128!' },
    @{ Input = 'title-card.png'; Output = 'startup.png'; Size = '280x158!' },
    @{ Input = 'title-card.png'; Output = 'pic0.png'; Size = '960x544!' }
)
foreach ($target in $targets) {
    # Artwork comes from imagegen. Only perform required size/format conversion.
    $inputPath = Join-Path $masterDir $target.Input
    $outputPath = Join-Path $assetDir $target.Output
    & magick $inputPath -filter Lanczos -resize $target.Size -strip -alpha off -dither FloydSteinberg -colors 256 "PNG8:$outputPath"
    if ($LASTEXITCODE -ne 0) { throw "Asset conversion failed: $inputPath" }
}
