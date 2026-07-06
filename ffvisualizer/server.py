"""aiohttp server: serves the dashboard and streams FF state over WebSocket."""
from __future__ import annotations

import asyncio
import json
import os
from aiohttp import web, WSMsgType

import proxy as proxy_mod
import sysfs_gains

WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
SAMPLE_HZ = 60


class AppState:
    def __init__(self, dev_path: str, observe: bool):
        self.dev_path = dev_path
        self.observe = observe
        self.proxy = proxy_mod.make_proxy(dev_path, observe)
        self.clients: set = set()
        self.thread = None

    def start(self) -> None:
        import threading
        self.thread = threading.Thread(target=self.proxy.run, daemon=True)
        self.thread.start()

    def stop(self) -> None:
        self.proxy.stop()


async def _sample_broadcaster(app: web.Application) -> None:
    state: AppState = app["state"]
    period = 1.0 / SAMPLE_HZ
    try:
        while True:
            if state.clients:
                snap = await asyncio.to_thread(state.proxy.sample)
                msg = json.dumps(snap)
                dead = set()
                for ws in state.clients:
                    try:
                        await ws.send_str(msg)
                    except Exception:
                        dead.add(ws)
                state.clients -= dead
            await asyncio.sleep(period)
    except asyncio.CancelledError:
        pass


async def websocket_handler(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    request.app["state"].clients.add(ws)
    try:
        async for msg in ws:
            if msg.type == WSMsgType.ERROR:
                break
    finally:
        request.app["state"].clients.discard(ws)
    return ws


async def devices_handler(request: web.Request) -> web.Response:
    return web.json_response({"devices": proxy_mod.list_ffb_devices()})


async def gains_get_handler(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    return web.json_response(
        {"gains": sysfs_gains.probe(state.dev_path), "observe": state.observe})


async def gains_set_handler(request: web.Request) -> web.Response:
    state: AppState = request.app["state"]
    try:
        data = await request.json()
        key = data["key"]
        value = int(data["value"])
    except (KeyError, ValueError, json.JSONDecodeError):
        return web.json_response({"ok": False, "error": "bad request"}, status=400)
    ok = sysfs_gains.set_value(state.dev_path, key, value)
    return web.json_response({"ok": ok, "gains": sysfs_gains.probe(state.dev_path)})


def build_app(dev_path: str, observe: bool) -> web.Application:
    app = web.Application()
    state = AppState(dev_path, observe)
    app["state"] = state
    app.router.add_get("/ws", websocket_handler)
    app.router.add_get("/api/devices", devices_handler)
    app.router.add_get("/api/gains", gains_get_handler)
    app.router.add_post("/api/gains", gains_set_handler)
    app.router.add_static("/", WEB_DIR, show_index=True)

    async def on_startup(app):
        # Start the proxy thread (fast, returns immediately).
        state.start()
        # Schedule the websocket broadcaster as a background task. NOTE: the
        # broadcaster is an infinite loop, so we must store the Task and never
        # `await` it — aiohttp's Signal.send does `await receiver(app)`, and a
        # handler that returns the Task would hang startup forever, preventing
        # the TCP site from ever binding.
        app["broadcaster"] = asyncio.create_task(_sample_broadcaster(app))

    async def on_cleanup(app):
        state.stop()
        task = app.get("broadcaster")
        if task is not None:
            task.cancel()

    app.on_startup.append(on_startup)
    app.on_cleanup.append(on_cleanup)
    return app
