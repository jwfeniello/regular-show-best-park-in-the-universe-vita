"""Draw the port's controller icons from vector shapes (requires ImageMagick)."""
from pathlib import Path
import subprocess

OUT = Path(__file__).resolve().parents[1] / 'extras/prompts'
OUT.mkdir(parents=True, exist_ok=True)
outline = '<circle cx="32" cy="32" r="27" fill="#202432" stroke="#fff" stroke-width="3"/>'
shapes = {
    'square': '<path d="M20 20H44V44H20Z" stroke="#ff99d4"/>',
    'cross': '<path d="M20 20L44 44M44 20L20 44" stroke="#98ceff"/>',
    'triangle': '<path d="M32 17L48 45H16Z" stroke="#87edc7"/>',
    'r': '<path d="M23 46V18H34Q45 18 45 27Q45 34 34 34H23M34 34L46 46" stroke="#fff"/>',
    'l': '<path d="M24 18V45H43" stroke="#fff"/>',
    'stick': '<circle cx="32" cy="32" r="10" stroke="#fff"/><path d="M32 8L27 14H37ZM32 56L27 50H37ZM8 32L14 27V37ZM56 32L50 27V37Z" fill="#fff" stroke="none"/>',
}

def symbol(name):
    # Vita L/R prompts use wide shoulder-button badges, as in Sony's manual:
    # https://playstation-doc.net/j/gravitydaze2/remote.html
    if name in ('l', 'r'):
        badge = '<rect x="2" y="14" width="60" height="36" rx="4" fill="#202432" stroke="#fff" stroke-width="3"/>'
        return badge + '<g transform="translate(6.4 6.4) scale(.8)" fill="none" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round">' + shapes[name] + '</g>'
    return outline + '<g fill="none" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round">' + shapes[name] + '</g>'

def save(name, width, height, content):
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">{content}</svg>\n'
    src, dest = OUT / (name + '.svg'), OUT / (name + '.png')
    src.write_text(svg, encoding='utf-8')
    subprocess.run(['magick', '-background', 'none', str(src), '-strip', 'PNG32:' + str(dest)], check=True)

for name in ('square', 'cross', 'triangle', 'stick', 'r'):
    save(name, 64, 64, symbol(name))
# Match the original instruction cards' untrimmed 173x78 canvas.
for name, glyph, x in (('help_move','stick',0), ('help_attack','square',40),
                       ('help_tag','l',41), ('help_super','r',34)):
    save(name, 173, 78, f'<g transform="translate({x} 7)">{symbol(glyph)}</g>')
print('Prepared 9 controller icons in', OUT)
