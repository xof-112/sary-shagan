#!/usr/bin/env python3
"""Baut docs/sary-shagan-handbuch.html aus den Quellen in diesem Ordner.

    make handbuch          (aus dem Repo-Wurzelordner; erzeugt vorher Bilder und Parameterliste)

Quellen:
    kapitel.html   der Text, Kapitel getrennt durch <!--KAPITEL id=… gruppe=… titel=…-->
    style.html     Hausstil (wie Taurids, Theme „Kante“), unverändert übernommen
    vorlesen.html  Vorlese-Baustein, unverändert übernommen (Handbuch-Leitfaden)
    wirkung.tsv    Kurzbeschreibung je Parameter für den Anhang

Erzeugt wird, was sich erzeugen lässt (Leitfaden: „Was sich erzeugen lässt, wird erzeugt“):
    {{SCR:Titelanfang|Bildunterschrift}}  Display-Bild aus render/frames (make render)
    {{VSCR:Titelanfang|Bildunterschrift}} Display-Bild aus dem VCV-Host (vcv/test/frames.txt)
    {{PANEL|Bildunterschrift}}            Panel aus vcv/test/panel-preview.png
    {{ANHANG}}                            Parametertabelle aus render/parameter.tsv (make list)
    {{VER}} {{VCV}} {{DATUM}} {{NPAR}}    Version (Plug-in), VCV-Version, Stand, Parameterzahl
Kapitelnummern, Inhaltsverzeichnis und „↑ zum Inhalt“ setzt das Skript selbst.
"""
import base64
import datetime
import glob
import html
import io
import json
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(ROOT, 'docs', 'sary-shagan-handbuch.html')
DATUM = os.environ.get('HANDBUCH_DATUM') or datetime.date.today().strftime('%d.%m.%Y')


def read(p):
    return open(p, encoding='utf-8').read()


def esc(s):
    return html.escape(s, quote=True)


# ---- Daten aus dem Plug-in ------------------------------------------------------
rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(ROOT, 'render', 'parameter.tsv'), encoding='utf-8')]
VER, NPAR = rows[0][1], rows[0][2]
params = [r[1:] for r in rows[1:] if r[0] == 'P']
assert len(params) == int(NPAR), 'Parameterliste unvollständig'
VCV = json.load(open(os.path.join(ROOT, 'vcv', 'plugin.json')))['version']
wirkung = {}
for l in open(os.path.join(HERE, 'wirkung.tsv'), encoding='utf-8'):
    if l.strip() and not l.startswith('#'):
        k, v = l.rstrip('\n').split('\t', 1)
        wirkung[k] = v
missing = [p[1] for p in params if p[1] not in wirkung]
assert not missing, 'wirkung.tsv fehlt: %s' % missing


def png_uri(img, scale):
    img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    bio = io.BytesIO()
    img.save(bio, 'PNG', optimize=True)
    return 'data:image/png;base64,' + base64.b64encode(bio.getvalue()).decode()


frames = []  # (Titel, Bild) aus test/host --render
for pgm in sorted(glob.glob(os.path.join(ROOT, 'render', 'frames', 'screen_*.pgm'))):
    frames.append((read(pgm[:-4] + '.txt').strip(), Image.open(pgm).convert('L')))
vframes = []  # aus vcv/test/host_test (MENU u. a.)
vf = os.path.join(ROOT, 'vcv', 'test', 'frames.txt')
if os.path.exists(vf):
    cur = None
    for line in open(vf, encoding='utf-8'):
        if line.startswith('F '):
            cur = [line[2:].strip(), []]
            vframes.append(cur)
        elif line.startswith('P ') and cur:
            cur[1].append([int(c, 16) * 17 for c in line[2:].strip()])


def screen(img, cap, alt):
    rgb = Image.merge('RGB', (img, img, img))
    return ('<figure class="scr"><img src="%s" alt="%s" width="1024" height="256"><figcaption>%s</figcaption></figure>'
            % (png_uri(rgb, 4), esc('NT-Display: ' + alt), cap))


def scr(m):
    key, cap = m.group(1).split('|', 1)
    for t, img in frames:
        if t.startswith(key):
            return screen(img, cap, t)
    sys.exit('Kein Bildschirm beginnt mit: ' + key)


def vscr(m):
    key, cap = m.group(1).split('|', 1)
    for t, px in vframes:
        if t.startswith(key):
            img = Image.new('L', (256, 64))
            img.putdata([v for r in px for v in r])
            return screen(img, cap, t)
    sys.exit('Kein VCV-Bildschirm beginnt mit: ' + key + ' (vorher make -C vcv/test)')


def panel(m):
    p = Image.open(os.path.join(ROOT, 'vcv', 'test', 'panel-preview.png')).convert('RGB')
    bio = io.BytesIO()
    p.save(bio, 'PNG', optimize=True)
    uri = 'data:image/png;base64,' + base64.b64encode(bio.getvalue()).decode()
    return ('<figure class="scr"><img src="%s" alt="Panel des VCV-Moduls Sary-Shagan" width="%d" height="%d" '
            'style="max-width:%dpx"><figcaption>%s</figcaption></figure>' % (uri, p.width, p.height, p.width, m.group(1)))


def anhang():
    out = ['<div class="tbl"><table>', '<tr><th>Seite</th><th>Parameter</th><th>Bereich</th><th>Standard</th><th>Wirkung</th></tr>']
    last = None
    for page, name, rng, std in params:
        out.append('<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>' % (
            esc(page) if page != last else '', esc(name), esc(rng), esc(std), wirkung[name]))
        last = page
    out.append('</table></div>')
    return '\n'.join(out)


# ---- Kapitel ------------------------------------------------------------------------
src = read(os.path.join(HERE, 'kapitel.html'))
for k, v in {'VER': VER, 'VCV': VCV, 'DATUM': DATUM, 'NPAR': NPAR}.items():
    src = src.replace('{{%s}}' % k, v)
src = re.sub(r'\{\{SCR:(.*?)\}\}', scr, src)
src = re.sub(r'\{\{VSCR:(.*?)\}\}', vscr, src)
src = re.sub(r'\{\{PANEL\|(.*?)\}\}', panel, src)
src = src.replace('{{ANHANG}}', anhang())
left = [x for x in re.findall(r'\{\{[^}]*\}\}', src) if not x.startswith('{{KAP:')]
assert not left, 'Platzhalter übrig: %s' % left

head, *parts = re.split(r'<!--KAPITEL (.*?)-->', src)
chapters = []
for i in range(0, len(parts), 2):
    attrs = dict(re.findall(r'(\w+)="(.*?)"', parts[i]))
    chapters.append((attrs, parts[i + 1].strip()))

toc, cards, group = [], [], None
for n, (a, body) in enumerate(chapters, 1):
    if a['gruppe'] != group:
        if group is not None:
            toc.append('</ol>')
        group = a['gruppe']
        toc.append('<p class="toc-grp">%s</p><ol>' % group)
    subs = re.findall(r'<h3 id="([^"]+)">(.*?)</h3>', body)
    short = dict(re.findall(r'<h3 id="([^"]+)"[^>]*data-kurz="([^"]+)"', body))
    sub = ''
    if subs:
        sub = '<div class="ts">' + ' · '.join(
            '<a class="sl" href="#%s">%s</a>' % (i, short.get(i, re.sub('<[^>]+>', '', t))) for i, t in subs) + '</div>'
    toc.append('<li><a class="tl" href="#%s"><span class="tn">%02d</span><span class="tt">%s</span></a>%s</li>'
               % (a['id'], n, a['titel'], sub))
    cards.append('<article class="patch-card" id="%s">\n  <div class="ph"><div class="num">%02d</div><div class="ph-main"><h2>%s</h2></div></div>\n'
                 '  <div class="pb">\n%s\n  <a class="backtop" href="#inhalt">↑ zum Inhalt</a></div>\n</article>'
                 % (a['id'], n, a['titel'], body))
toc.append('</ol>')

# Kapitelverweise: {{KAP:id}} -> „Kapitel 07“ mit Link
nums = {a['id']: n for n, (a, _) in enumerate(chapters, 1)}
body = '\n\n'.join(cards)
head = head.strip()


def kap(m):
    i = m.group(1)
    return '<a href="#%s">Kapitel %02d</a>' % (i, nums[i])


body = re.sub(r'\{\{KAP:([\w-]+)\}\}', kap, body)
head = re.sub(r'\{\{KAP:([\w-]+)\}\}', kap, head)

page = ('<!doctype html>\n<html lang="de">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">\n'
        '<title>Sary-Shagan – Handbuch</title>\n'
        '<link rel="preconnect" href="https://fonts.googleapis.com">\n'
        '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n'
        '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700&family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&display=swap">\n'
        + read(os.path.join(HERE, 'style.html')) + '\n</head>\n<body>\n<div class="wrap">\n' + head
        + '\n\n<nav id="inhalt" class="toc" aria-label="Inhalt"><p class="eyebrow">Inhalt</p>\n' + '\n'.join(toc) + '\n</nav>\n\n<section>\n'
        + body + '\n</section>\n\n'
        + '<footer>Stand: Sary-Shagan v%s, VCV-Modul 3F4-SaryShagan %s, %s · Anhang und Display-Bilder aus dem Plug-in erzeugt '
          '(<code>test/host.cpp --list / --render</code>, <code>vcv/test/host_test.cpp</code>, <code>docs/handbuch/build.py</code>) · '
          'Quelltext: <a href="https://github.com/xof-112/sary-shagan" target="_blank" rel="noopener">github.com/xof-112/sary-shagan</a> · '
          '<a href="#inhalt">↑ zum Inhalt</a></footer>\n</div>\n\n' % (VER, VCV, DATUM)
        + read(os.path.join(HERE, 'vorlesen.html')) + '\n</body>\n</html>\n')
open(OUT, 'w', encoding='utf-8').write(page)
print('%s: %d Kapitel, %d Parameter, %.0f KB' % (os.path.relpath(OUT, ROOT), len(chapters), len(params), len(page) / 1024))
