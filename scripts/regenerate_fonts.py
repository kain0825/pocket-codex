"""Regenerate redistributable Noto subsets; requires npm-installed lv_font_conv."""
from pathlib import Path
import argparse, json, subprocess

ROOT=Path(__file__).resolve().parents[1]

def main(converter):
    glyphs=json.loads((ROOT/'assets/fonts/glyphs.json').read_text(encoding='utf-8'))
    for name,size,bpp in [('launcher_chinese_24',24,4),('codex_pixel_28',28,1)]:
        subprocess.run(['node',str(converter),'--font','assets/fonts/NotoSansCJKsc-Regular.otf',
            '--symbols',glyphs[name],'-r','32-126','--size',str(size),'--bpp',str(bpp),
            '--format','lvgl','--no-compress','--no-prefilter','--lv-include','lvgl.h',
            '--lv-font-name',name,'-o','firmware/main/display/'+name+'.c'],cwd=ROOT,check=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--converter',required=True,type=Path)
    main(p.parse_args().converter.resolve())
