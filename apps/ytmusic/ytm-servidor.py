#!/usr/bin/env python3
"""Servidor de YouTube Music para la app nativa.
Lee órdenes por stdin, una por línea, y responde por stdout:
  buscar <texto>   -> R <id>\t<título>\t<artista>\t<duración>  ... y FIN
  url <id>         -> U <id>\t<url de audio>  o  E <id>\t<error>
  radio <id>       -> R ... FIN con canciones relacionadas (cola automática)
"""
import json, sys, threading, urllib.request, queue

API = 'https://music.youtube.com/youtubei/v1/'
CTX = {'client': {'clientName': 'WEB_REMIX', 'clientVersion': '1.20261101.01.00', 'hl': 'es', 'gl': 'ES'}}
FILTRO_CANCIONES = 'EgWKAQIIAWoQEAMQBBAJEAoQBRAREBAQFQ%3D%3D'
salida = threading.Lock()

def di(*a):
    with salida:
        print(*a, flush=True)

def api(fin, cuerpo):
    cuerpo['context'] = CTX
    req = urllib.request.Request(API + fin + '?prettyPrint=false', json.dumps(cuerpo).encode(),
        {'Content-Type': 'application/json', 'User-Agent': 'Mozilla/5.0', 'Origin': 'https://music.youtube.com'})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)

def textos(col):
    return ''.join(x.get('text', '') for x in col.get('text', {}).get('runs', []))

def pistas_busqueda(datos):
    def recorre(n):
        if isinstance(n, dict):
            if 'musicResponsiveListItemRenderer' in n:
                yield n['musicResponsiveListItemRenderer']
            else:
                for v in n.values(): yield from recorre(v)
        elif isinstance(n, list):
            for v in n: yield from recorre(v)
    for it in recorre(datos):
        vid = it.get('playlistItemData', {}).get('videoId')
        cols = [c.get('musicResponsiveListItemFlexColumnRenderer', {}) for c in it.get('flexColumns', [])]
        if not vid or not cols: continue
        titulo = textos(cols[0])
        partes = [p.strip() for p in (textos(cols[1]) if len(cols) > 1 else '').split('•')]
        # partes: [Canción,] artista, álbum, duración
        if partes and partes[0] in ('Canción', 'Song', 'Vídeo', 'Video'): partes = partes[1:]
        dur = partes[-1] if partes and ':' in partes[-1] else ''
        yield vid, titulo, partes[0] if partes else '', dur

def pistas_radio(datos):
    def recorre(n):
        if isinstance(n, dict):
            if 'playlistPanelVideoRenderer' in n:
                yield n['playlistPanelVideoRenderer']
            else:
                for v in n.values(): yield from recorre(v)
        elif isinstance(n, list):
            for v in n: yield from recorre(v)
    for it in recorre(datos):
        art = ''.join(r.get('text', '') for r in it.get('longBylineText', {}).get('runs', [])).split('•')[0].strip()
        yield it['videoId'], ''.join(r['text'] for r in it['title']['runs']), art, it.get('lengthText', {}).get('runs', [{}])[0].get('text', '')

def limpia(s): return s.replace('\t', ' ').replace('\n', ' ')

def lista(gen):
    vistos = set()
    for vid, t, a, d in gen:
        if vid in vistos: continue
        vistos.add(vid)
        di('R', '\t'.join(map(limpia, (vid, t, a, d))))
        if len(vistos) >= 25: break
    di('FIN')

ydl = None
urls = {}
trabajo = queue.Queue()

def resolutor():
    global ydl
    import yt_dlp
    ydl = yt_dlp.YoutubeDL({'quiet': True, 'no_warnings': True, 'format': 'bestaudio/best', 'noplaylist': True})
    while True:
        vid = trabajo.get()
        if vid not in urls:
            try:
                urls[vid] = ydl.extract_info('https://www.youtube.com/watch?v=' + vid, download=False)['url']
            except Exception as e:
                di('E', vid + '\t' + limpia(str(e).split('\n')[0][-120:]))
                continue
        di('U', vid + '\t' + urls[vid])

threading.Thread(target=resolutor, daemon=True).start()
for linea in sys.stdin:
    orden, _, arg = linea.strip().partition(' ')
    try:
        if orden == 'buscar':
            lista(pistas_busqueda(api('search', {'query': arg, 'params': FILTRO_CANCIONES})))
        elif orden == 'radio':
            lista(pistas_radio(api('next', {'videoId': arg, 'playlistId': 'RDAMVM' + arg})))
        elif orden == 'url':
            if arg in urls: di('U', arg + '\t' + urls[arg])
            else: trabajo.put(arg)
    except Exception as e:
        di('X', limpia(str(e))[:150]); di('FIN')
