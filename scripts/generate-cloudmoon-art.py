#!/usr/bin/env python3
"""Generate CloudMoon PS3 XMB artwork using Pillow (no external image assets)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math, sys

out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets")
out.mkdir(parents=True, exist_ok=True)

def font(size, bold=False):
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    ]
    for p in candidates:
        try: return ImageFont.truetype(p, size)
        except OSError: pass
    return ImageFont.load_default()

def background(w, h):
    im = Image.new("RGB", (w,h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            glow = max(0, 1 - math.sqrt(((x-w*.72)/(w*.8))**2 + ((y-h*.8)/(h*.8))**2))
            top = y / max(1,h-1)
            px[x,y] = (int(2+5*glow), int(7+18*glow), int(22+75*glow+8*top))
    d = ImageDraw.Draw(im, "RGBA")
    # Subtle stars and orbital light ribbons.
    for i in range(95):
        x = (i*193 + 71) % w; y = (i*97 + 29) % h
        r = 1 if i%5 else 2
        d.ellipse((x-r,y-r,x+r,y+r), fill=(90,170,255,75))
    for offset, alpha, width in [(0,95,5),(22,55,3),(45,30,2)]:
        pts=[]
        for x in range(-20,w+20,12):
            y=int(h*.58 + math.sin(x/w*math.pi*2+offset*.025)*h*.12 + (x/w)*h*.12)
            pts.append((x,y+offset))
        d.line(pts, fill=(20,105,255,alpha), width=width)
    return im

def draw_mark(im, box, scale=1.0, label=True):
    x0,y0,x1,y1=box
    d=ImageDraw.Draw(im, "RGBA")
    cx=(x0+x1)//2; cy=(y0+y1)//2
    # Glowing orbital ring.
    ring=(x0+8,cy-int((y1-y0)*.20),x1-8,cy+int((y1-y0)*.20))
    d.ellipse(ring, outline=(0,145,255,180), width=max(2,int(5*scale)))
    # Metallic K with restrained cyan edge.
    f=font(int((y1-y0)*.73), True)
    bb=d.textbbox((0,0),"K",font=f,stroke_width=max(1,int(2*scale)))
    tw=bb[2]-bb[0]; th=bb[3]-bb[1]
    tx=cx-tw//2; ty=cy-th//2-int((y1-y0)*.03)
    d.text((tx+3,ty+4),"K",font=f,fill=(0,35,100,220),stroke_width=max(2,int(5*scale)),stroke_fill=(0,130,255,220))
    d.text((tx,ty),"K",font=f,fill=(235,247,255,255),stroke_width=max(1,int(2*scale)),stroke_fill=(65,165,255,255))
    if label:
        lf=font(max(10,int((y1-y0)*.14)),True)
        t="CLOUDMOON"
        b=d.textbbox((0,0),t,font=lf)
        d.text((cx-(b[2]-b[0])//2,y1-int((y1-y0)*.17)),t,font=lf,fill=(210,231,255,245))

# ICON0.PNG: PS3's 320x176 XMB icon aspect ratio.
icon=background(320,176)
draw_mark(icon,(35,8,285,164),0.52,True)
if not (out/"ICON0.PNG").exists():
    icon.save(out/"ICON0.PNG",format="PNG",optimize=True)

# PIC1.PNG: wide XMB background, 1920x1080.
pic=background(1920,1080)
draw_mark(pic,(120,130,760,800),2.0,False)
d=ImageDraw.Draw(pic,"RGBA")
d.text((835,350),"CloudMoon",font=font(116,True),fill=(232,244,255,255),stroke_width=2,stroke_fill=(0,75,180,180))
d.text((845,495),"PLAY YOUR GAMES. ANYWHERE.",font=font(38,True),fill=(80,190,255,245))
d.text((845,575),"CLOUD GAMING  •  PLAYSTATION 3",font=font(25),fill=(155,190,235,220))
# Soft light bloom behind the emblem and title.
pic=pic.filter(ImageFilter.GaussianBlur(0.25))
if not (out/"PIC1.PNG").exists():
    pic.save(out/"PIC1.PNG",format="PNG",optimize=True)
print("Preserved existing artwork; generated only missing ICON0.PNG/PIC1.PNG files.")
