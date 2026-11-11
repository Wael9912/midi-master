#!/usr/bin/env python3
"""Bundle the app into ONE self-contained HTML file (dist/midi-master.html).

Use it to share the app as a single file (WhatsApp, Telegram, a Claude artifact)
or to open it from the phone's file manager. The modules are concatenated in
dependency order with their import/export lines stripped; CSS and icons are
inlined. The service worker and manifest are left out (a single file has no
origin to install into), everything else is identical to the served app.

    python3 tools/build_single.py            # → dist/midi-master.html
    python3 tools/build_single.py --artifact # → dist/midi-master-artifact.html (no <html>/<head>/<body>, for claude.ai)
"""
import base64, re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
ORDER = ['chord', 'styledsl', 'styles', 'sty', 'orgset', 'sequencer', 'synth', 'sf2', 'orgsampler', 'midi', 'audio', 'engine', 'store', 'i18n', 'catalog', 'sheet', 'app']

def strip_module(src: str) -> str:
    src = re.sub(r'^import\s[^;]*;\s*$', '', src, flags=re.M)
    src = re.sub(r'^export\s+(async function|const|function|class|let)\b', r'\1', src, flags=re.M)
    return src

NAMESPACES = {  # modules app.js imports as `import * as name`
    'store': 'const store = { loadSettings, saveSettings, putFile, getFile, deleteFile };',
}

js = '\n'.join(f'// ───── js/{m}.js ─────\n' + strip_module((ROOT / 'js' / f'{m}.js').read_text()) + '\n' + NAMESPACES.get(m, '') for m in ORDER)
css = (ROOT / 'css' / 'app.css').read_text()
html = (ROOT / 'index.html').read_text()
icon = base64.b64encode((ROOT / 'icons' / 'icon-192.png').read_bytes()).decode()
icon_uri = f'data:image/png;base64,{icon}'

# Single-file: no service worker, no manifest; icons inline; one inline script.
html = html.replace('<link rel="manifest" href="manifest.webmanifest">\n', '')
html = html.replace('<link rel="stylesheet" href="css/app.css">', f'<style>\n{css}\n</style>')
html = html.replace('icons/icon-192.png', icon_uri)
html = html.replace('<script type="module" src="js/app.js"></script>', f'<script>\n{js}\n</script>')
js_sw_off = "if ('serviceWorker' in navigator && location.protocol !== 'file:')"
assert js_sw_off in html
html = html.replace(js_sw_off, 'if (false)')

artifact = '--artifact' in sys.argv
out = ROOT / 'dist'
out.mkdir(exist_ok=True)
if artifact:
    # The claude.ai publisher wraps the file in its own skeleton: keep only head + body content.
    head = re.search(r'<head>(.*?)</head>', html, re.S).group(1)
    body = re.search(r'<body>(.*?)</body>', html, re.S).group(1)
    head = re.sub(r'\s*<meta name="viewport"[^>]*>', '', head)
    head = re.sub(r'\s*<meta charset="utf-8">', '', head)
    page = head.strip() + '\n' + body.strip() + '\n'
    path = out / 'midi-master-artifact.html'
else:
    page = html
    path = out / 'midi-master.html'
path.write_text(page)
print(f'{path} ({len(page.encode()) // 1024} KB)')
