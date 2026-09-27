"""Capacity harness for an isolated formal online server and test accounts.

Requires Python 3.11+ and websockets 16. Writes only aggregate measurements.
No account secret, hand, or message body is logged.
"""

import argparse
import asyncio
import json
import statistics
import time
import uuid
from pathlib import Path

from websockets.asyncio.client import connect


class ProtocolError(RuntimeError):
    def __init__(self, code):
        super().__init__(code)
        self.code = code


TEST_PASSWORD = "capacity-test-only-2026"

# Argon2id registration/login hashing runs on the server's event loop; keep the
# one-time account burst small so the measured game phase stays uncontended.
AUTH_LIMITER = asyncio.Semaphore(4)


class Client:
    def __init__(self, account):
        self.account = account
        self.token = None
        self.socket = None
        self.pending = {}
        self.view = None
        self.invitation = None
        self.reader = None
        self.latencies = []
        self.read_error = None

    async def open(self, uri, connect_host=None):
        address = {"host": connect_host, "port": 443} if connect_host else {}
        self.socket = await connect(uri, max_size=65536, ping_interval=20,
                                    open_timeout=30, proxy=None, **address)
        self.reader = asyncio.create_task(self.read())
        # The server hashes passwords with Argon2id on its single event loop, so
        # a burst of new accounts is intentionally throttled here. Registration
        # is a one-time cost; the measured gameplay gate stays fully concurrent.
        if self.token:
            reply = await self.request("resume", payload={"token": self.token},
                                       timeout=60)
        else:
            async with AUTH_LIMITER:
                # Keep the one-time account burst under the server's per-IP
                # auth limit; the measured game phase is unaffected.
                await asyncio.sleep(0.2)
                try:
                    reply = await self.request("login", payload={
                        "username": self.account, "password": TEST_PASSWORD},
                        timeout=60)
                except ProtocolError as exc:
                    if exc.code != "invalid_credentials":
                        raise
                    reply = await self.request("register", payload={
                        "username": self.account, "nickname": self.account,
                        "password": TEST_PASSWORD}, timeout=60)
        self.token = reply["payload"]["token"]

    async def read(self):
        try:
            async for raw in self.socket:
                message = json.loads(raw)
                if message.get("type") == "view":
                    self.view = message.get("payload")
                if message.get("type") == "invite":
                    self.invitation = message.get("payload")
                key = message.get("request_id")
                future = self.pending.get(key)
                if future and not future.done():
                    future.set_result(message)
        except Exception as exc:
            self.read_error = type(exc).__name__

    async def request(self, kind, room=None, payload=None, seq=None, round_id=None,
                      request_id=None, timeout=10):
        request_id = request_id or str(uuid.uuid4())
        message = {
            "version": 1,
            "type": kind,
            "request_id": request_id,
            "room_id": room,
            "round_id": round_id,
            "seq": seq,
            "payload": payload or {},
        }
        future = asyncio.get_running_loop().create_future()
        self.pending[request_id] = future
        started = time.perf_counter()
        try:
            await self.socket.send(json.dumps(message, separators=(",", ":")))
            try:
                response = await asyncio.wait_for(future, timeout)
            except TimeoutError as exc:
                raise RuntimeError(f"request timeout: kind={kind} room={room} "
                                   f"seq={seq} reader_error={self.read_error}") from exc
        finally:
            self.pending.pop(request_id, None)
        self.latencies.append((kind, (time.perf_counter() - started) * 1000))
        if response["type"] == "error":
            raise ProtocolError(response["payload"]["code"])
        return response

    async def room_seq(self, room):
        count = int(room[0])
        reply = await self.request("list", payload={"player_count": count})
        return next(item["seq"] for item in reply["payload"]["rooms"]
                    if item["room_id"] == room)

    async def close(self, abrupt=False):
        if self.socket is None:
            return
        if abrupt:
            # Model an unplanned outage: the service keeps the seat for its
            # 30-second reconnect window instead of releasing it at once.
            self.socket.transport.abort()
            self.socket = None
        else:
            await self.socket.close()
        if self.reader:
            try:
                await asyncio.wait_for(self.reader, 5)
            except Exception:
                pass
        self.socket = None


class RoomRunner:
    def __init__(self, size, number):
        self.id = f"{size}-{number:02d}"
        self.players = [Client(f"load_{size}_{number:02d}_{i}") for i in range(size)]
        self.actions = 0
        self.auto_steps = 0
        self.rounds = 0
        self.stale_races = 0
        self.chat_sent = 0
        self.reconnections = 0
        self.uri = None
        self.connect_host = None

    async def setup(self, uri, connect_host=None):
        self.uri = uri
        self.connect_host = connect_host
        await asyncio.gather(*(player.open(uri, connect_host) for player in self.players))
        for player in self.players:
            await player.request("join", self.id, seq=await player.room_seq(self.id))
        current = await self.players[0].request("resync", self.id)
        if current["payload"]["view"]["status"] != "playing":
            await self.start()
        for player in self.players:
            state = await player.request("resync", self.id)
            view = state["payload"]["view"]
            assert "randomSeed" not in view and "actionHistory" not in view
            assert all("hand" not in seat for seat in view["seats"])
            if not view.get("bottom_revealed", False):
                assert "bottom_cards" not in view

    async def start(self):
        host = self.players[0]
        reply = await host.request("resync", self.id)
        if reply["payload"]["view"]["status"] == "playing":
            return
        try:
            await host.request("start", self.id, seq=reply["seq"])
        except ProtocolError as exc:
            # Another seat can win the race between the two reads; a rejected
            # start is a race, not a service fault.
            if exc.code not in {"already_started", "stale_seq", "stale_round"}:
                raise
            self.stale_races += 1
            return
        self.rounds += 1

    async def step(self, timeout_fraction):
        host = self.players[0]
        current = await host.request("resync", self.id)
        view = current["payload"]["view"]
        if view["status"] == "finished" or view["status"] == "waiting":
            await self.start()
            return
        actor = view["current_player"]
        player = self.players[actor]
        if timeout_fraction and self.actions % round(1 / timeout_fraction) == 0:
            self.auto_steps += 1
            await asyncio.sleep(view["turn_seconds"] + 0.6)
            self.actions += 1
            return
        own = await player.request("resync", self.id)
        view = own["payload"]["view"]
        if view["status"] != "playing" or view["phase"] not in (2, 4):
            # The round can finish between the two reads; start the next one
            # exactly as the room rules require instead of treating the settled
            # round as an error.
            await self.start()
            return
        if view["phase"] == 2:
            kind, payload = "bid", {"score": 3}
        else:
            if not view["last_played_cards"] or actor == view["last_played_by"]:
                kind, payload = "play", {"card_ids": [view["hand"][0]]}
            else:
                kind, payload = "pass", {}
        try:
            await player.request(kind, self.id, payload=payload,
                                 seq=own["seq"], round_id=own["round_id"])
        except ProtocolError as exc:
            # A deliberate timeout hands the seat to the bot, and a stale room
            # revision rejects the late client action. Both are the designed
            # automatic-play paths, so they are counted, not treated as errors.
            if exc.code in {"stale_seq", "stale_round", "not_actor", "bot_deciding"}:
                self.stale_races += 1
                return
            raise
        self.actions += 1
        if self.actions % 30 == 0:
            try:
                await player.request("chat", self.id, {"text": "load ping"})
                self.chat_sent += 1
            except ProtocolError as exc:
                if exc.code != "chat_limited":
                    raise

    async def run(self, until, timeout_fraction, reconnect_at=None):
        while time.monotonic() < until:
            if reconnect_at is not None and time.monotonic() >= reconnect_at:
                player = self.players[-1]
                await player.close(abrupt=True)
                await asyncio.sleep(0.5)
                await player.open(self.uri, self.connect_host)
                state = await player.request("resync", self.id)
                assert state["payload"]["view"]["self_seat"] >= 0
                self.reconnections += 1
                reconnect_at = None
            await self.step(timeout_fraction)
            await asyncio.sleep(0.1)

    async def close(self):
        await asyncio.gather(*(p.close() for p in self.players))


async def invite_worker(index, uri, connect_host, until):
    host = Client(f"invite_host_{index}")
    guest = Client(f"invite_guest_{index}")
    room = f"3-{13 + index:02d}" if index < 8 else f"4-{index - 1:02d}"
    sent = 0
    try:
        await asyncio.gather(host.open(uri, connect_host),
                             guest.open(uri, connect_host))
        await host.request("join", room, seq=await host.room_seq(room))
        roster = await host.request("list_players")
        target = next(item["account"] for item in roster["payload"]["players"]
                      if item["nickname"] == guest.account)
        while time.monotonic() < until:
            await host.request("invite", room, {"scope": "target", "account": target})
            sent += 1
            await asyncio.sleep(min(60, max(0, until - time.monotonic())))
        current = await host.request("resync", room)
        await host.request("leave", room, seq=current["seq"])
    finally:
        await asyncio.gather(host.close(), guest.close(), return_exceptions=True)
    return sent


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uri", default="ws://127.0.0.1:19763",
                        help="One URI or comma-separated independent tunnel URIs")
    parser.add_argument("--connect-host", help="Connect to this IP while preserving URI TLS host")
    parser.add_argument("--minutes", type=float, default=0.1)
    parser.add_argument("--timeout-fraction", type=float, default=0.1)
    parser.add_argument("--rooms", type=int, default=38)
    parser.add_argument("--room-offset", type=int, default=0,
                        help="Skip this many rooms in the 100-player layout for short tests")
    parser.add_argument("--exercise-paths", action="store_true")
    parser.add_argument("--list-only", action="store_true",
                        help="Show public room IDs, occupancy and status, then exit")
    parser.add_argument("--output", type=Path, help="Write aggregate JSON under the project build directory")
    args = parser.parse_args()
    if args.list_only:
        inspector = Client("load_inspector")
        try:
            await inspector.open(args.uri.split(",")[0], args.connect_host)
            for count in (2, 3, 4):
                response = await inspector.request("list", payload={"player_count": count})
                print(json.dumps([{key: room[key] for key in
                    ("room_id", "occupied", "status", "seq")}
                    for room in response["payload"]["rooms"]], ensure_ascii=False))
        finally:
            await inspector.close()
        return
    layout = [(2, i) for i in range(1, 21)] + [
        (3, i) for i in range(1, 13)] + [(4, i) for i in range(1, 7)]
    runners = [RoomRunner(*entry) for entry in
               layout[args.room_offset:args.room_offset + args.rooms]]
    if len(runners) != args.rooms:
        parser.error("room selection exceeds the fixed 38-room capacity layout")
    uris = args.uri.split(",")
    began = time.time()
    try:
        await asyncio.gather(*(room.setup(uris[index % len(uris)], args.connect_host)
                               for index, room in enumerate(runners)))
        if args.exercise_paths:
            host, recipient = Client("probe_host"), Client("probe_recipient")
            await asyncio.gather(host.open(uris[0], args.connect_host),
                                 recipient.open(uris[0], args.connect_host))
            probe_room = "3-20"
            before_join = {"seq": await host.room_seq(probe_room)}
            first_join = await host.request("join", probe_room,
                                            seq=before_join["seq"])
            replayed = await host.request("join", probe_room,
                                          seq=before_join["seq"],
                                          request_id=first_join["request_id"])
            assert replayed == first_join, "request deduplication changed reply"
            roster = await host.request("list_players")
            target = next(item["account"] for item in roster["payload"]["players"]
                          if item["nickname"] == recipient.account)
            await host.request("invite", probe_room,
                               {"scope": "target", "account": target})
            for _ in range(50):
                if recipient.invitation:
                    break
                await asyncio.sleep(0.01)
            assert recipient.invitation, "invitation not delivered"
            token = recipient.invitation["invitation_id"]
            current = {"seq": await recipient.room_seq(probe_room)}
            await recipient.request("join", probe_room,
                                    {"invitation_id": token}, seq=current["seq"])
            before = await host.request("resync", probe_room)
            try:
                await host.request("start", probe_room, seq="-1")
                raise AssertionError("stale sequence was accepted")
            except ProtocolError as exc:
                assert exc.code == "stale_seq"
            await host.request("chat", probe_room, {"text": "capacity probe"})
            after = await recipient.request("resync", probe_room)
            assert before["seq"] == after["seq"], "chat changed game sequence"
            assert after["payload"]["recent_messages"][-1]["text"] == "capacity probe"
            await host.close(abrupt=True)
            await asyncio.sleep(0.1)
            await host.open(uris[0], args.connect_host)
            reconnected = await host.request("resync", probe_room)
            assert reconnected["payload"]["view"]["occupied"] == 2
            current = await host.request("resync", probe_room)
            await host.request("leave", probe_room, seq=current["seq"])
            current = await recipient.request("resync", probe_room)
            await recipient.request("leave", probe_room, seq=current["seq"])
            await asyncio.gather(host.close(), recipient.close())
        until = time.monotonic() + args.minutes * 60
        reconnect_at = time.monotonic() + args.minutes * 20
        runs = [room.run(until, args.timeout_fraction,
                         reconnect_at if index < 10 else None)
                for index, room in enumerate(runners)]
        invites = [invite_worker(index, uris[index % len(uris)],
                                 args.connect_host, until)
                   for index in range(10)] if args.exercise_paths else []
        invite_results = await asyncio.gather(*runs, *invites)
    finally:
        await asyncio.gather(*(room.close() for room in runners), return_exceptions=True)
    samples = [sample for room in runners for player in room.players
               for sample in player.latencies]
    auth_latencies = [duration for kind, duration in samples
                      if kind in {"register", "login", "resume"}]
    latencies = [duration for kind, duration in samples
                 if kind not in {"register", "login", "resume"}]
    action_latencies = [duration for kind, duration in samples
                        if kind in {"bid", "play", "pass", "start", "join"}]
    gameplay_latencies = [duration for kind, duration in samples
                          if kind in {"bid", "play", "pass"}]
    join_latencies = [duration for kind, duration in samples if kind == "join"]
    start_latencies = [duration for kind, duration in samples if kind == "start"]
    def percentile(values, fraction):
        if not values:
            return None
        ordered = sorted(values)
        return ordered[int(fraction * (len(ordered) - 1))]
    def rounded_p95(values):
        value = percentile(values, 0.95)
        return round(value, 2) if value is not None else None
    result = {
        "duration_seconds": round(time.time() - began, 1),
        "connected_players": sum(len(room.players) for room in runners),
        "rooms": len(runners),
        "actions": sum(room.actions for room in runners),
        "automatic_steps_requested": sum(room.auto_steps for room in runners),
        "rounds_started": sum(room.rounds for room in runners),
        "stale_races_rejected": sum(room.stale_races for room in runners),
        "chat_messages_sent": sum(room.chat_sent for room in runners),
        "batch_reconnections": sum(room.reconnections for room in runners),
        "invitations_sent": sum(invite_results[len(runners):]),
        "requests": len(latencies),
        "confirmation_p50_ms": round(statistics.median(latencies), 2),
        "confirmation_p95_ms": rounded_p95(latencies),
        "confirmation_max_ms": round(max(latencies), 2),
        "state_change_requests": len(action_latencies),
        "state_change_p95_ms": rounded_p95(action_latencies),
        "gameplay_p95_ms": rounded_p95(gameplay_latencies),
        "account_auth_p95_ms": rounded_p95(auth_latencies),
        "join_p95_ms": rounded_p95(join_latencies),
        "start_p95_ms": rounded_p95(start_latencies),
    }
    rendered = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        output = args.output.resolve()
        build_root = (Path(__file__).resolve().parents[1] / "build").resolve()
        if not output.is_relative_to(build_root):
            parser.error("--output must be inside the repository build directory")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)


if __name__ == "__main__":
    asyncio.run(main())
