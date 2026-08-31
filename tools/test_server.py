#!/usr/bin/env python3
"""mTLS WSS echo server for the fountainer_client testbed.

Replaces the not-yet-existing firmware_server of the ESP32-S3 at the
transport level: WSS + mTLS against the testbed CA, subprotocol "fountain",
text frames only. On connection it sends a greeting and then echoes
every text message back.

The testbed's server key is encrypted with a dummy password; it comes
from the environment variable TLS_KEY_PASSWORD.
"""

import argparse
import asyncio
import logging
import os
import ssl

from websockets.asyncio.server import serve

log = logging.getLogger("test_server")


def build_ssl_context(ca: str, cert: str, key: str) -> ssl.SSLContext:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2

    context.load_cert_chain(
        certfile=cert,
        keyfile=key,
        password=os.environ.get("TLS_KEY_PASSWORD"),
    )

    # mTLS: require a client certificate and verify it against the root CA
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(cafile=ca)

    return context


async def handle(websocket) -> None:
    try:
        peer_cert = websocket.transport.get_extra_info("peercert") or {}
    except AttributeError:
        peer_cert = {}
    subject = dict(
        item for sub in peer_cert.get("subject", ()) for item in sub
    )

    log.info(
        "client connected: %s (subprotocol=%s)",
        subject.get("commonName", "<no cert subject>"),
        websocket.subprotocol,
    )

    await websocket.send("hello from test_server")

    try:
        async for message in websocket:
            if isinstance(message, bytes):
                log.warning("binary frame ignored (%d bytes)", len(message))
                continue

            log.info("echoing %d bytes: %.200s", len(message), message)

            await websocket.send(message)
    finally:
        log.info("client disconnected")


async def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=4443)
    parser.add_argument("--ca", required=True)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)-5s] %(name)s %(message)s",
    )

    ssl_context = build_ssl_context(args.ca, args.cert, args.key)

    async with serve(
        handle,
        args.host,
        args.port,
        ssl=ssl_context,
        subprotocols=["fountain"],
    ):
        log.info("listening on wss://%s:%d/ws (mTLS)", args.host, args.port)

        await asyncio.get_running_loop().create_future()  # run forever


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
