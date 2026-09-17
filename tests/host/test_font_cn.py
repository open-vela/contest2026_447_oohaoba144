"""Validate generated C bytes independently against the installed source font."""
from pathlib import Path
import re, unittest
from PIL import ImageFont
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT.parent / "apps/graphics/lvgl/lvgl/tests/src/test_files/fonts/noto"
ASSET = ROOT / "app/velaguard/src/velaguard_font_cn16.c"

class FontTests(unittest.TestCase):
    def test_generated_font(self):
        self.assertTrue(ASSET.is_file(), "Missing generated font asset")
        text = ASSET.read_text(encoding="utf-8")
        def array(name):
            found = re.search(r"\b"+name+r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
            self.assertIsNotNone(found, name)
            return found[1]
        bitmap = bytes(int(v,16) for v in re.findall(r"0x([0-9a-f]{2})", array("glyph_bitmap")))
        mapping = [int(v,16)+32 for v in re.findall(r"0x([0-9a-f]{4})", array("unicode_list"))]
        entries = [tuple(map(int,v)) for v in re.findall(r"\{(\d+),(\d+),(\d+),(\d+),(-?\d+),(-?\d+)\}",array("glyph_dsc"))]
        chars = set(range(32,127))
        for hi in range(0xa1,0xf8):
            for lo in range(0xa1,0xff):
                try: chars.add(ord(bytes((hi,lo)).decode("gb2312")))
                except UnicodeDecodeError: pass
        self.assertEqual(mapping, sorted(chars)); self.assertEqual(len(mapping),7540)
        self.assertEqual(len(entries),7541); self.assertEqual(entries[0],(0,0,0,0,0,0))
        self.assertLess(len(bitmap),1<<20)
        font=ImageFont.truetype(str(SOURCE / "NotoSansSC-Regular.ttf"),16)
        ascent,descent=font.getmetrics()
        self.assertIn(".line_height = %d" % (ascent+descent),text)
        self.assertIn(".base_line = %d" % descent,text)
        cursor=0
        for cp,entry in zip(mapping,entries[1:]):
            start,advance,width,height,x,y=entry
            mask,offset=font.getmask2(chr(cp),mode="L",anchor="ls")
            self.assertEqual((width,height),mask.size); self.assertEqual((x,y),(offset[0],-offset[1]-height))
            self.assertEqual(advance,round(font.getlength(chr(cp))*16)); self.assertEqual(start,cursor)
            count=width*height; packed=bitmap[start:start+(count+1)//2]
            pixels=[n for b in packed for n in (b>>4,b&15)][:count]
            self.assertEqual(len(pixels),count)
            self.assertTrue(all(abs(a*17-b)<=9 for a,b in zip(pixels,bytes(mask))),hex(cp))
            if count%2:self.assertEqual(packed[-1]&15,0)
            self.assertLessEqual(width,255); self.assertLessEqual(height,255)
            self.assertTrue(-128<=x<=127 and -128<=y<=127)
            cursor+=(count+1)//2
        self.assertEqual(cursor,len(bitmap))
        copied_license = (ROOT / "app/velaguard/fonts/OFL-Noto.txt").read_text(encoding="utf-8")
        source_license = (SOURCE / "OFL.txt").read_text(encoding="utf-8")
        normalize = lambda value: "\n".join(line.rstrip() for line in value.splitlines())
        self.assertEqual(normalize(copied_license), normalize(source_license))
        print("PASS glyphs=%d bitmap=%d line=%d baseline=%d source-pixel-error<=9" % (len(mapping),len(bitmap),ascent+descent,descent))

if __name__ == "__main__": unittest.main()
