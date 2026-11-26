#!/usr/bin/env python3
"""Original 2:1 equirect ball textures + sphere preview tiles. V=0 at the north pole (top). U is periodic."""
import math, os, json, struct, hashlib
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageEnhance

W, H = 2048, 1024
PREV = 512

def hash2(x, y, s=0):
    n = np.sin((x * 127.1 + y * 311.7 + s * 74.7) * 43758.5453)
    return n - np.floor(n)

def fade(t):
    return t * t * t * (t * (t * 6 - 15) + 10)

def value_noise(x, y, seed):

    x0 = np.floor(x).astype(np.int32); y0 = np.floor(y).astype(np.int32)
    xf = x - x0; yf = y - y0
    u, v = fade(xf), fade(yf)
    def h(i, j):
        return hash2(i, j, seed)
    n00 = h(x0, y0); n10 = h(x0+1, y0); n01 = h(x0, y0+1); n11 = h(x0+1, y0+1)
    nx0 = n00 * (1-u) + n10 * u
    nx1 = n01 * (1-u) + n11 * u
    return nx0 * (1-v) + nx1 * v

def fbm(x, y, seed, octaves=6, lac=2.0, gain=0.5):
    amp, freq, tot, nrm = 1.0, 1.0, 0.0, 0.0
    for o in range(octaves):
        tot += amp * value_noise(x*freq, y*freq, seed + o * 19)
        nrm += amp
        amp *= gain; freq *= lac
    return tot / nrm

def sph_grid(w=W, h=H):

    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    U, V = np.meshgrid(u, v)
    lon = (U - 0.5) * 2 * math.pi
    lat = (0.5 - V) * math.pi
    cl, sl = np.cos(lat), np.sin(lat)
    x = cl * np.sin(lon)
    y = sl
    z = cl * np.cos(lon)
    return U, V, lon, lat, x, y, z

def wrap_blur(img, r=1):
    """Blur that wraps on U (horizontal) and clamps on V."""
    arr = np.array(img)
    pad = max(r*3, 8)
    left = arr[:, :pad].copy(); right = arr[:, -pad:].copy()
    arr2 = np.concatenate([right, arr, left], axis=1)
    im = Image.fromarray(arr2)
    im = im.filter(ImageFilter.GaussianBlur(radius=r))
    out = np.array(im)[:, pad:-pad]
    return Image.fromarray(out)

def to_img(rgb):
    rgb = np.clip(rgb, 0, 1)
    return Image.fromarray((rgb * 255 + 0.5).astype(np.uint8), 'RGB')


def make_galaxy():
    U, V, lon, lat, x, y, z = sph_grid()



    px = np.cos(lon) * np.cos(lat)
    py = np.sin(lon) * np.cos(lat)
    pz = y
    n1 = fbm(px*3.0, py*3.0 + pz*1.7, 11, 7)
    n2 = fbm(px*5.5 + 2.2, pz*5.5, 29, 6)
    n3 = fbm(px*8.0, py*8.0, 47, 5)

    axis = np.array([0.35, 0.15, 0.92]); axis = axis / np.linalg.norm(axis)
    dplane = np.abs(x*axis[0] + y*axis[1] + z*axis[2])
    band = np.exp(-((dplane)*3.2)**2)

    dens = np.clip((n1 - 0.35) * 1.8, 0, 1) ** 1.1
    dens = dens * 0.55 + band * (0.35 + 0.65 * n2)
    dens = np.clip(dens, 0, 1)

    void = np.array([0.03, 0.02, 0.08])
    indigo = np.array([0.12, 0.05, 0.32])
    violet = np.array([0.42, 0.12, 0.72])
    magenta = np.array([0.70, 0.18, 0.55])
    teal = np.array([0.10, 0.55, 0.62])
    gold = np.array([0.95, 0.62, 0.28])
    pink = np.array([0.95, 0.45, 0.70])
    t = n2
    col = void * (1-dens)[...,None]
    col = col + indigo * (dens * (1-t) * 0.8)[...,None]
    col = col + violet * (dens * t * 0.7)[...,None]
    col = col + magenta * (np.clip(n3-0.45,0,1) * dens * 0.55)[...,None]
    col = col + teal * (np.clip(0.55-n1,0,1) * dens * 0.35)[...,None]
    col = col + gold * (band * np.clip(n2-0.5,0,1) * 0.55)[...,None]
    col = col + pink * (band * np.clip(n1-0.6,0,1) * 0.25)[...,None]

    core = np.exp(-((dplane)*5.0)**2) * np.clip(n3, 0, 1)
    col = col + np.array([1.0, 0.92, 0.85]) * (core * 0.35)[...,None]

    rngx = np.arange(W); rngy = np.arange(H)
    XX, YY = np.meshgrid(rngx, rngy)
    hs = hash2(XX * 0.37, YY * 1.13, 91)
    hs2 = hash2(XX * 1.9, YY * 0.51, 3)
    star = (hs > 0.9975).astype(np.float64) * (0.6 + 0.4 * hs2)
    star += (hs2 > 0.9993).astype(np.float64)

    col = col + star[...,None] * (0.85 + 0.15 * np.stack([hs, hs2, 1-hs], -1))
    col = np.clip(col, 0, 1)

    col = np.clip((col - 0.02) * 1.08, 0, 1)
    return to_img(col)


def brighten_galaxy(img, gamma=0.50, lift=0.05, indigo_amt=0.35):
    """Lift the authored Galaxy equirect so the nebula reads under toon shading.

    Applied to the existing original art (does not regenerate stars or clouds).
    Rec. 709 mean luminance goes from ~0.125 to ~0.332, in line with Fire (~0.329).
    Shadows get a small indigo fill so crushed voids become a visible deep navy
    rather than a grey lift. Highlights and stars stay at 1.
    """
    rgb = np.asarray(img.convert('RGB'), dtype=np.float32) / 255.0
    Y = 0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]
    Y2 = lift + (1.0 - lift) * np.power(np.clip(Y, 0.0, 1.0), gamma)
    scale = np.divide(Y2, np.maximum(Y, 1e-6))
    out = rgb * scale[..., None]
    indigo = np.array([0.16, 0.09, 0.40], dtype=np.float32)
    w = np.clip((0.05 - Y) / 0.05, 0.0, 1.0)[..., None]
    out = np.clip(out + indigo * (Y2 * indigo_amt)[..., None] * w, 0.0, 1.0)
    return to_img(out)


def make_fire():
    U, V, lon, lat, x, y, z = sph_grid()
    px, py, pz = np.cos(lon)*np.cos(lat), np.sin(lon)*np.cos(lat), y
    n = fbm(px*4.0, py*4.0 + pz*2.5, 7, 7, 2.05, 0.52)
    n2 = fbm(px*9.0, pz*9.0, 21, 6)
    n3 = fbm(px*18.0, py*18.0, 35, 4)

    ridge = 1.0 - np.abs(n - 0.48) * 2.2
    ridge = np.clip(ridge, 0, 1) ** 2.2
    cracks = np.clip(ridge * 1.4 + (1-n2)*0.15, 0, 1)

    crust = np.array([0.07, 0.04, 0.035])
    dark = np.array([0.14, 0.06, 0.04])
    ember = np.array([0.85, 0.18, 0.02])
    lava = np.array([1.0, 0.45, 0.05])
    hot = np.array([1.0, 0.92, 0.45])
    white = np.array([1.0, 0.98, 0.85])
    heat = cracks * (0.55 + 0.45 * n3)
    col = crust * (1-heat*0.2)[...,None]
    col = col + dark * ((1-heat) * n2 * 0.5)[...,None]
    col = col + ember * (np.clip(heat,0,1) * 0.7)[...,None]
    col = col + lava * (np.clip(heat-0.35,0,1) * 1.1)[...,None]
    col = col + hot * (np.clip(heat-0.7,0,1) * 1.2)[...,None]
    col = col + white * (np.clip(heat-0.88,0,1) * 1.1)[...,None]

    XX, YY = np.meshgrid(np.arange(W), np.arange(H))
    sp = hash2(XX*0.71, YY*1.3, 5)
    sparks = (sp > 0.996) & (heat > 0.3)
    col[sparks] = np.clip(col[sparks] + np.array([1.0, 0.8, 0.3]), 0, 1)
    return to_img(np.clip(col, 0, 1))


def make_beach():
    U, V, lon, lat, x, y, z = sph_grid()

    n_gores = 8
    colors = np.array([
        [0.89, 0.16, 0.14],
        [0.97, 0.97, 0.95],
        [0.98, 0.78, 0.10],
        [0.97, 0.97, 0.95],
        [0.12, 0.40, 0.86],
        [0.97, 0.97, 0.95],
        [0.98, 0.78, 0.10],
        [0.97, 0.97, 0.95],
    ])

    ang = (lon + math.pi) / (2*math.pi)
    g = np.floor(ang * n_gores).astype(np.int32) % n_gores

    edge = np.abs((ang * n_gores) - np.round(ang * n_gores))
    seam_w = 0.018
    seam = np.clip(1.0 - edge / seam_w, 0, 1)
    cap = 18.0 * math.pi/180.0
    is_cap = np.abs(lat) > (math.pi/2 - cap)
    cap_col = np.array([0.97, 0.97, 0.95])
    cap_seam = np.clip(1.0 - np.abs(np.abs(lat) - (math.pi/2 - cap)) / 0.02, 0, 1)
    col = colors[g]
    navy = np.array([0.08, 0.12, 0.22])
    col = col * (1-seam)[...,None] + navy * seam[...,None]
    col = np.where(is_cap[...,None], cap_col, col)
    col = col * (1-cap_seam)[...,None] + navy * cap_seam[...,None]

    shade = 0.97 + 0.03 * np.cos(ang * n_gores * 2 * math.pi)
    col = col * shade[...,None]
    return to_img(col)


def make_tricolor():
    U, V, lon, lat, x, y, z = sph_grid()


    cols = np.array([
        [0.96, 0.96, 0.94],
        [0.12, 0.25, 0.62],
        [0.90, 0.68, 0.12],
    ])

    band = np.zeros_like(lat, dtype=np.int32)
    band[lat > math.radians(30)] = 2
    band[lat < math.radians(-30)] = 0
    band[(lat >= math.radians(-30)) & (lat <= math.radians(30))] = 1

    ang = (lon + math.pi) / (2*math.pi)
    stagger = np.where(band == 1, 0.0, 0.5/6.0)
    a = (ang + stagger) % 1.0
    gore = np.floor(a * 6).astype(np.int32) % 6

    cid = (gore + band * 2) % 3

    edge_g = np.abs((a * 6) - np.round(a * 6))
    seam_g = edge_g < (0.022)

    seam_g = edge_g < 0.035

    seam_b = (np.abs(lat - math.radians(30)) < math.radians(1.4)) | (np.abs(lat - math.radians(-30)) < math.radians(1.4))
    seam = seam_g | seam_b
    ink = np.array([0.10, 0.11, 0.14])
    col = cols[cid]
    col = np.where(seam[...,None], ink, col)

    px, py = np.cos(lon)*np.cos(lat), np.sin(lon)*np.cos(lat)
    grain = 0.97 + 0.03 * fbm(px*12, py*12 + y*8, 4, 4)
    col = col * grain[...,None]
    return to_img(np.clip(col, 0, 1))

def render_preview(tex: Image.Image, yaw_deg=35, pitch_deg=-12, size=PREV) -> Image.Image:
    """Orthographic-ish sphere preview, game-like key light, transparent-ish dark bg -> opaque tile."""
    tex = tex.convert('RGB')
    tw, th = tex.size
    arr = np.array(tex, dtype=np.float32) / 255.0
    yaw = math.radians(yaw_deg); pitch = math.radians(pitch_deg)

    light = np.array([-0.45, 0.62, 0.65], dtype=np.float64)
    light = light / np.linalg.norm(light)

    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    def rot(v):
        x,y,z = v

        x,z = x*cy + z*sy, -x*sy + z*cy

        y,z = y*cp - z*sp, y*sp + z*cp
        return np.array([x,y,z])
    bg = np.array([0.10, 0.11, 0.14])
    out = np.zeros((size, size, 3), dtype=np.float64)

    yy, xx = np.mgrid[0:size, 0:size]
    ndc_x = (xx + 0.5) / size * 2 - 1
    ndc_y = 1 - (yy + 0.5) / size * 2
    r = 0.86
    rr = ndc_x**2 + ndc_y**2
    mask = rr <= r*r
    z = np.sqrt(np.clip(r*r - rr, 0, None))

    n = np.stack([ndc_x/r, ndc_y/r, z/r], -1)


    ns = n.copy()

    y2 = ns[...,1]*cp + ns[...,2]*sp
    z2 = -ns[...,1]*sp + ns[...,2]*cp
    ns[...,1], ns[...,2] = y2, z2

    x2 = ns[...,0]*cy - ns[...,2]*sy
    z2 = ns[...,0]*sy + ns[...,2]*cy
    ns[...,0], ns[...,2] = x2, z2

    u = (np.arctan2(ns[...,0], ns[...,2]) / (2*math.pi) + 0.5) % 1.0
    v = np.arccos(np.clip(ns[...,1], -1, 1)) / math.pi
    px = np.clip((u * tw).astype(np.int32), 0, tw-1)
    py = np.clip((v * th).astype(np.int32), 0, th-1)
    albedo = arr[py, px]

    ndl = np.clip(n @ light, -1, 1)
    band_edge, shade, soft = 0.45, 0.34, 0.04

    t = np.clip((ndl - (band_edge-soft)) / max(2*soft, 1e-4), 0, 1)
    lit = t*t*(3-2*t)
    shade_m = (1-shade) * (1-lit) + 1.0 * lit
    col = albedo * shade_m[...,None]

    view = np.array([0,0,1])
    ndv = np.clip(n @ view, 0, 1)
    edge = np.sqrt(np.clip(1 - ndv*ndv, 0, 1))
    rim = np.clip((edge - 0.82) / 0.12, 0, 1)
    col = col + rim[...,None] * 0.18
    out[mask] = np.clip(col[mask], 0, 1)

    out[~mask] = bg

    ring = (rr > r*r) & (rr < (r*1.045)**2)
    out[ring] = np.array([0.05, 0.05, 0.07])
    return to_img(out)

def write_pack(root: Path, folder, pack_id, name, label, img: Image.Image):
    d = root / folder
    d.mkdir(parents=True, exist_ok=True)
    tex_path = d / 'ball_tex.png'
    img.save(tex_path, optimize=True)
    prev = render_preview(img)
    prev.save(d / 'preview.png', optimize=True)
    skin = {
        "id": pack_id,
        "name": name,
        "label": label,
        "kind": "global",
        "type": "ball",
        "preview": "preview.png",
        "ball": {
            "albedo_tex": "ball_tex.png",
            "wrap": "equirect",
            "albedo_color": "#ffffff"
        }
    }
    (d / 'skin.json').write_text(json.dumps(skin, indent=2) + '\n')
    print(f'wrote {d} {img.size} preview={prev.size} sha_tex={hashlib.sha256(tex_path.read_bytes()).hexdigest()[:16]}')

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--lift-galaxy', action='store_true',
                    help='Brighten the existing 06_galaxy_ball art in place (do not regenerate).')
    args = ap.parse_args()
    out = Path(__file__).resolve().parents[1] / 'resource_packs'
    dist = Path(__file__).resolve().parents[1] / 'dist' / 'game_dir' / 'mods' / 'resources' / 'packs'
    if args.lift_galaxy:
        src = out / '06_galaxy_ball' / 'ball_tex.png'

        print('lifting existing Galaxy (keep art, raise void/nebula luminance)...')
        img = brighten_galaxy(Image.open(src).convert('RGB'))

        for root in (out, dist):
            d = root / '06_galaxy_ball'
            d.mkdir(parents=True, exist_ok=True)
            img.save(d / 'ball_tex.png', optimize=True)
            render_preview(img).save(d / 'preview.png', optimize=True)
            print('updated', d)
        return
    print('galaxy...')
    g = make_galaxy(); write_pack(out, '06_galaxy_ball', '1006', 'Galaxy', 'Galaxy', g)
    print('fire...')
    f = make_fire(); write_pack(out, '10_fire_lava_ball', '1010', 'Fire & Lava', 'Fire', f)
    print('beach...')
    b = make_beach(); write_pack(out, '11_beach_ball', '1011', 'Beach Ball', 'Beach', b)
    print('tricolor...')
    t = make_tricolor(); write_pack(out, '12_tricolor_ball', '1012', 'Classic Tri-Colour', 'Tri-Colour', t)
    print('done')

if __name__ == '__main__':
    main()
