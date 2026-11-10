#!/usr/bin/env python3
"""Proxy HTTP mínimo para que el Moto E instale paquetes a través del USB.

Escucha solo en 127.0.0.1 del PC; llega al móvil con `ssh -R`.
Solo deja pasar GET/HEAD por HTTP a los repositorios de paquetes.
"""
import asyncio
from urllib.parse import urlsplit

PERMITIDOS = {
    "mirror.nura.eco",
    "mirror.postmarketos.org",
    "cdn.alpinelinux.org",
    "dl-cdn.alpinelinux.org",
}
PUERTO = 3128


async def copiar(lector, escritor):
    try:
        while datos := await lector.read(65536):
            escritor.write(datos)
            await escritor.drain()
    finally:
        escritor.close()


async def atender(lector, escritor):
    try:
        cabecera = await lector.readuntil(b"\r\n\r\n")
        lineas = cabecera.decode("latin-1").split("\r\n")
        metodo, url, version = lineas[0].split(" ")
        partes = urlsplit(url)
        if metodo not in ("GET", "HEAD") or partes.scheme != "http" or partes.hostname not in PERMITIDOS:
            escritor.write(b"HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n")
            await escritor.drain()
            escritor.close()
            return
        ruta = partes.path or "/"
        if partes.query:
            ruta += "?" + partes.query
        resto = [
            l for l in lineas[1:]
            if l and not l.lower().startswith(("connection:", "proxy-connection:"))
        ]
        peticion = "\r\n".join([f"{metodo} {ruta} {version}", *resto, "Connection: close", "", ""])
        srv_lector, srv_escritor = await asyncio.open_connection(partes.hostname, partes.port or 80)
        srv_escritor.write(peticion.encode("latin-1"))
        await srv_escritor.drain()
        print(metodo, url, flush=True)
        await copiar(srv_lector, escritor)
        srv_escritor.close()
    except (asyncio.IncompleteReadError, ValueError, ConnectionError, OSError) as error:
        print("error:", error, flush=True)
        escritor.close()


async def main():
    servidor = await asyncio.start_server(atender, "127.0.0.1", PUERTO)
    async with servidor:
        await servidor.serve_forever()


asyncio.run(main())
