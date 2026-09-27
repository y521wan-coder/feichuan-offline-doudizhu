"""End-to-end protocol checks for an isolated, disposable online server.

Run only against a fresh local test database. The runner creates accounts and
changes rooms, and intentionally never prints passwords, tokens, chat text, or
card IDs. Install ``websockets>=16`` in the Python environment first.
"""

import argparse
import asyncio
import ipaddress
import json
import secrets
import sys
import time
import uuid
from urllib.parse import urlsplit

from websockets.asyncio.client import connect


class ProtocolError(AssertionError):
    def __init__(self, code, response):
        super().__init__(code)
        self.code = code
        self.response = response


class Peer:
    def __init__(self, uri, name):
        self.uri = uri
        self.name = name
        self.ws = None
        self.reader = None
        self.pending = {}
        self.events = asyncio.Queue()
        self.token = None
        self.short_id = None

    async def open(self):
        self.ws = await connect(self.uri, proxy=None, open_timeout=15,
                                max_size=65536, ping_interval=20)
        self.reader = asyncio.create_task(self._read())
        return self

    async def _read(self):
        try:
            async for raw in self.ws:
                message = json.loads(raw)
                request_id = message.get("request_id")
                future = self.pending.get(request_id)
                if future and not future.done():
                    future.set_result(message)
                else:
                    await self.events.put(message)
        except Exception as exc:
            for future in self.pending.values():
                if not future.done():
                    future.set_exception(exc)

    async def request(self, kind, *, room=None, payload=None, seq=None,
                      round_id=None, request_id=None, version=1):
        request_id = request_id or str(uuid.uuid4())
        message = {"version": version, "type": kind, "request_id": request_id,
                   "room_id": room, "round_id": round_id, "seq": seq,
                   "payload": payload or {}}
        future = asyncio.get_running_loop().create_future()
        self.pending[request_id] = future
        try:
            await self.ws.send(json.dumps(message, ensure_ascii=False,
                                          separators=(",", ":")))
            response = await asyncio.wait_for(future, 10)
        finally:
            self.pending.pop(request_id, None)
        assert response.get("request_id") == request_id, kind
        assert response.get("version") == 1, kind
        assert response.get("type") in {"ok", "error"}, kind
        if response["type"] == "error":
            raise ProtocolError(response["payload"]["code"], response)
        return response

    async def error(self, kind, code, **kwargs):
        try:
            await self.request(kind, **kwargs)
        except ProtocolError as exc:
            assert exc.code == code, f"{kind}: expected {code}, got {exc.code}"
            return exc.response
        raise AssertionError(f"{kind}: expected {code}")

    async def event(self, kind, timeout=5):
        stop = time.monotonic() + timeout
        while True:
            remaining = stop - time.monotonic()
            if remaining <= 0:
                raise AssertionError(f"{self.name}: no {kind} event")
            message = await asyncio.wait_for(self.events.get(), remaining)
            if message.get("type") == kind:
                return message

    async def close(self, abrupt=False):
        if self.ws is None:
            return
        if abrupt:
            self.ws.transport.abort()
        else:
            await self.ws.close()
        if self.reader:
            try:
                await asyncio.wait_for(self.reader, 5)
            except Exception:
                pass
        self.ws = None
        self.reader = None


class Checks:
    def __init__(self, uri):
        self.uri = uri
        self.prefix = "qa" + secrets.token_hex(4)
        self.password = secrets.token_urlsafe(18)
        self.room_password = secrets.token_urlsafe(18)
        self.peers = []
        self.count = 0

    def passed(self, name):
        self.count += 1
        print(f"PASS {name}", flush=True)

    async def peer(self, suffix):
        peer = await Peer(self.uri, self.prefix + suffix).open()
        self.peers.append(peer)
        return peer

    async def register(self, suffix, nickname=None):
        peer = await self.peer(suffix)
        answer = await peer.request("register", payload={
            "username": peer.name, "nickname": nickname or suffix,
            "password": self.password})
        peer.token = answer["payload"]["token"]
        assert len(peer.token) == 64
        return peer

    async def state(self, peer, room):
        answer = await peer.request("resync", room=room)
        view = answer["payload"]["view"]
        self.private_view(view)
        assert answer["seq"] == view["seq"]
        return answer

    async def room_seq(self, peer, room):
        count = int(room.split("-", 1)[0])
        answer = await peer.request("list", payload={"player_count": count})
        return next(item["seq"] for item in answer["payload"]["rooms"]
                    if item["room_id"] == room)

    @staticmethod
    def private_view(view):
        assert "hand" not in str(view.get("seats", []))
        assert "randomSeed" not in view and "random_seed" not in view
        assert "actionHistory" not in view and "action_history" not in view
        assert "full_state" not in view and "deck" not in view
        assert "password" not in view and "password_hash" not in view
        if not view.get("bottom_revealed", False):
            assert "bottom_cards" not in view
        if "hand" in view:
            assert isinstance(view["hand"], list)
            assert len(set(view["hand"])) == len(view["hand"])

    async def action(self, peer, kind, room, payload=None, request_id=None):
        if kind == "join":
            seq, round_id = await self.room_seq(peer, room), None
        else:
            old = await self.state(peer, room)
            seq, round_id = old["seq"], old["round_id"]
        return await peer.request(kind, room=room, payload=payload,
                                  seq=seq, round_id=round_id, request_id=request_id)

    async def run(self):
        anonymous = await self.peer("anon")
        await anonymous.error("list", "login_required", payload={"player_count": 2})
        await anonymous.error("list", "protocol_version", version=2)
        await anonymous.error("register", "invalid_registration", payload={
            "username": "ab", "nickname": "昵称", "password": self.password})
        await anonymous.error("register", "invalid_registration", payload={
            "username": self.prefix + "short", "nickname": "昵称", "password": "short"})
        await anonymous.error("register", "invalid_registration", payload={
            "username": self.prefix + "longname", "nickname": "过" * 17,
            "password": self.password})
        await anonymous.close()
        self.passed("anonymous, protocol-version, and registration-boundary rejection")

        host = await self.register("host", "房主")
        guest = await self.register("guest", "朋友")
        outsider = await self.register("outsider", "旁观者")
        invitee = await self.register("invitee", "朋友")
        await (await self.peer("duplicate")).error(
            "register", "username_taken", payload={"username": host.name.upper(),
            "nickname": "重复", "password": self.password})
        await (await self.peer("duplicate_login")).error(
            "login", "account_connected", payload={"username": host.name,
            "password": self.password})
        self.passed("registration normalization and one live login")

        all_rooms = []
        empty_rooms = {}
        for count in (2, 3, 4):
            reply = await host.request("list", payload={"player_count": count})
            rooms = reply["payload"]["rooms"]
            assert len(rooms) == 20
            assert {r["room_id"] for r in rooms} == {
                f"{count}-{number:02d}" for number in range(1, 21)}
            assert all(r["player_count"] == count for r in rooms)
            all_rooms.extend(rooms)
            empty_rooms[count] = [r["room_id"] for r in rooms
                                  if r["occupied"] == 0 and r["status"] == "waiting"]
        assert len(all_rooms) == 60
        assert len(empty_rooms[2]) >= 4 and empty_rooms[3] and empty_rooms[4]
        self.passed("60 fixed rooms across three player counts")

        room = empty_rooms[2][-1]
        race_room = empty_rooms[2][-4]
        await self.action(host, "join", room)
        await self.action(host, "set_password", room,
                          {"password": self.room_password})
        await self.action(host, "set_turn_seconds", room, {"seconds": 30})
        await guest.error("join", "wrong_password", room=room,
                          seq=await self.room_seq(guest, room),
                          payload={"password": "invalid-password"})
        await self.action(guest, "join", room,
                          {"password": self.room_password})
        await outsider.error("join", "room_full", room=room,
                             seq=await self.room_seq(outsider, room))
        await host.error("set_password", "host_alone_required", room=room,
                         seq=(await self.state(host, room))["seq"],
                         payload={"password": self.room_password})
        self.passed("password room, host controls, and full-room rejection")

        await self.action(host, "start", room)
        before = await self.state(host, room)
        view = before["payload"]["view"]
        assert view["status"] == "playing" and view["phase"] == 2
        assert "bottom_cards" not in view
        actor = (host, guest)[view["current_player"]]
        other = guest if actor is host else host
        await other.error("bid", "not_actor", room=room,
                          seq=before["seq"], round_id=before["round_id"],
                          payload={"score": 3})
        await outsider.error("bid", "not_seated", room=room,
                             seq=before["seq"], round_id=before["round_id"],
                             payload={"score": 3})
        await actor.error("play", "illegal_action", room=room,
                          seq=before["seq"], round_id=before["round_id"],
                          payload={"card_ids": [255]})
        bid = await actor.request("bid", room=room, seq=before["seq"],
                                  round_id=before["round_id"],
                                  payload={"score": 3})
        assert bid["payload"]["view"]["phase"] == 4
        self.private_view(bid["payload"]["view"])
        self.passed("server action authorization, bidding, and hidden cards")

        current = await self.state(actor, room)
        current_view = current["payload"]["view"]
        assert current_view["hand"]
        await actor.error("play", "illegal_action", room=room,
                          seq=current["seq"], round_id=current["round_id"],
                          payload={"card_ids": [255]})
        play_id = str(uuid.uuid4())
        played = await actor.request("play", room=room,
                                    seq=current["seq"], round_id=current["round_id"],
                                    payload={"card_ids": [current_view["hand"][0]]},
                                    request_id=play_id)
        repeated = await actor.request("play", room=room,
                                      seq=current["seq"], round_id=current["round_id"],
                                      payload={"card_ids": [current_view["hand"][0]]},
                                      request_id=play_id)
        assert repeated == played
        stale = await actor.error("play", "stale_seq", room=room,
                                  seq=current["seq"], round_id=current["round_id"],
                                  payload={"card_ids": [current_view["hand"][0]]})
        assert stale["payload"]["view"]["seq"] == played["seq"]
        await actor.error("pass", "stale_round", room=room,
                          seq=played["seq"], round_id=str(uuid.uuid4()))
        self.passed("illegal card, persistent request dedupe, stale seq and round")

        chat_seq = (await self.state(host, room))["seq"]
        await host.request("chat", room=room, payload={"text": "<b>纯文本</b>"})
        event = await guest.event("chat")
        assert event["payload"]["text"] == "<b>纯文本</b>"
        outside = await outsider.error("resync", "not_seated", room=room)
        outside_text = json.dumps(outside, ensure_ascii=False)
        assert "<b>纯文本</b>" not in outside_text
        assert not outside["payload"].get("recent_messages")
        assert (await self.state(host, room))["seq"] == chat_seq
        await host.error("chat", "chat_limited", room=room,
                         payload={"text": "too fast"})
        await asyncio.sleep(1.1)
        await host.error("emote", "emote_limited", room=room,
                         payload={"id": 99})
        await host.request("emote", room=room, payload={"id": 1})
        assert (await guest.event("emote"))["payload"]["id"] == 1
        assert (await self.state(host, room))["seq"] == chat_seq
        diagnostic = await host.request("diagnostics")
        diagnostic_text = json.dumps(diagnostic, ensure_ascii=False)
        assert host.token not in diagnostic_text
        assert self.password not in diagnostic_text
        assert self.room_password not in diagnostic_text
        assert "hand" not in diagnostic_text and "card_ids" not in diagnostic_text
        assert "room_paused" in diagnostic["payload"]["diagnostics"]
        self.passed("room-only chat, approved emote, and private diagnostics")

        # A broken transport models an unplanned outage; normal close means
        # deliberate exit and has different seat semantics.
        old_token = guest.token
        await guest.close(abrupt=True)
        await asyncio.sleep(0.2)
        resumed = await self.peer("guest_resumed")
        reply = await resumed.request("resume", payload={"token": old_token})
        assert reply["payload"]["room_id"] == room
        assert reply["payload"]["view"]["self_seat"] >= 0
        self.private_view(reply["payload"]["view"])
        await resumed.request("logout")
        await resumed.close()
        await (await self.peer("revoked")).error("resume", "invalid_session",
                                                payload={"token": old_token})
        self.passed("unplanned reconnect and explicit token revocation")

        second_room = empty_rooms[2][-2]
        await self.action(outsider, "join", second_room)
        await self.action(outsider, "set_password", second_room,
                          {"password": self.room_password})
        probe = await self.register("password_probe", "密码探针")
        for _ in range(8):
            await probe.error("join", "wrong_password", room=second_room,
                              seq=await self.room_seq(probe, second_room),
                              payload={"password": "invalid-password"})
        await probe.error("join", "password_limited", room=second_room,
                          seq=await self.room_seq(probe, second_room),
                          payload={"password": self.room_password})
        self.passed("room password attempt limit")

        players = (await outsider.request("list_players"))["payload"]["players"]
        # Public IDs are the only target identifiers exposed by the lobby.
        same_nick = [p for p in players if p["nickname"] == "朋友"]
        assert len(same_nick) == 1 and same_nick[0]["account"] == same_nick[0]["short_id"]
        await invitee.request("set_invite_accept", payload={"enabled": False})
        hidden = (await outsider.request("list_players"))["payload"]["players"]
        assert not any(p["nickname"] == "朋友" for p in hidden)
        global_target = await self.register("global_target", "全服受邀者")
        # An earlier test run can leave a room-level 60-second invitation
        # cooldown even after that room becomes empty. Use another empty room
        # in that case so repeated regression runs remain independent.
        for candidate in reversed(empty_rooms[2][:-1]):
            if candidate == race_room:
                continue
            if candidate != second_room:
                await self.action(outsider, "leave", second_room)
                second_room = candidate
                await self.action(outsider, "join", second_room)
                await self.action(outsider, "set_password", second_room,
                                  {"password": self.room_password})
            try:
                global_reply = await outsider.request("invite", room=second_room,
                                                     payload={"scope": "all"})
                break
            except ProtocolError as exc:
                if exc.code != "invite_limited":
                    raise
        else:
            raise AssertionError("all available test rooms had invitation cooldowns")
        assert global_reply["payload"]["sent"] >= 1
        assert (await global_target.event("invite"))["payload"]["room_id"] == second_room
        await outsider.error("invite", "invite_limited", room=second_room,
                             payload={"scope": "all"})
        await asyncio.sleep(3.05)
        await invitee.request("set_invite_accept", payload={"enabled": True})
        public_id = next(p["account"] for p in (
            await outsider.request("list_players"))["payload"]["players"]
                         if p["nickname"] == "朋友")
        invited = await outsider.request("invite", room=second_room,
                                         payload={"scope": "player", "account": public_id})
        assert invited["payload"]["sent"] == 1
        invitation = (await invitee.event("invite"))["payload"]["invitation_id"]
        assert invitation and len(invitation) == 64
        await self.action(invitee, "join", second_room,
                          {"invitation_id": invitation})
        await self.action(invitee, "leave", second_room)
        await invitee.error("join", "invitation_expired", room=second_room,
                            seq=await self.room_seq(invitee, second_room),
                            payload={"invitation_id": invitation})
        self.passed("lobby privacy, global and targeted invites, password bypass and single use")

        # Messages are memory-only room history. Send enough to prove the
        # visible buffer is capped at twenty and each entry remains plain text.
        for index in range(21):
            for attempt in range(3):
                try:
                    await outsider.request("chat", room=second_room,
                                           payload={"text": f"message-{index:02d}"})
                    break
                except ProtocolError as exc:
                    if exc.code != "chat_limited" or attempt == 2:
                        raise AssertionError(
                            f"recent-message send {index}: {exc.code}") from exc
                    await asyncio.sleep(1.1)
            if index != 20:
                await asyncio.sleep(1.1)
        history = (await self.state(outsider, second_room))["payload"]["view"]["recent_messages"]
        assert len(history) == 20
        assert history[0]["text"] == "message-01"
        assert history[-1]["text"] == "message-20"
        await outsider.error("chat", "chat_limited", room=second_room,
                             payload={"text": "x" * 201})
        self.passed("recent 20 room messages and text limit")

        # An open room for each remaining player count proves the same engine
        # handles two, three, and four seats through the production protocol.
        for count in (3, 4):
            room_id = empty_rooms[count][-1]
            members = [await self.register(f"p{count}{i}", f"玩家{count}{i}")
                       for i in range(count)]
            for member in members:
                await self.action(member, "join", room_id)
            await self.action(members[0], "start", room_id)
            for member in members:
                reply = await self.state(member, room_id)
                state = reply["payload"]["view"]
                assert state["status"] == "playing"
                assert len(state["seats"]) == count
                assert len(state["hand"]) > 0
                assert "bottom_cards" not in state
        self.passed("two, three, and four player live-engine starts")

        race_host = await self.register("race_host", "抢位房主")
        first = await self.register("race_first", "抢位甲")
        second = await self.register("race_second", "抢位乙")
        await self.action(race_host, "join", race_room)
        race_seq = await self.room_seq(race_host, race_room)
        results = await asyncio.gather(
            first.request("join", room=race_room, seq=race_seq),
            second.request("join", room=race_room, seq=race_seq),
            return_exceptions=True)
        successes = [result for result in results if isinstance(result, dict)]
        errors = [result.code for result in results if isinstance(result, ProtocolError)]
        assert len(successes) == 1 and errors == ["room_full"], (
            f"last-seat race: successes={len(successes)}, errors={errors}")
        self.passed("last-seat arrival order and full loser prompt")

    async def close(self):
        await asyncio.gather(*(peer.close() for peer in self.peers),
                             return_exceptions=True)


async def main(uri):
    parts = urlsplit(uri)
    if parts.scheme not in {"ws", "wss"} or not parts.hostname:
        raise SystemExit("A ws:// or wss:// loopback URI is required")
    try:
        loopback = ipaddress.ip_address(parts.hostname).is_loopback
    except ValueError:
        loopback = parts.hostname.lower() == "localhost"
    if not loopback:
        raise SystemExit("This destructive test only accepts a loopback endpoint")
    checks = Checks(uri)
    try:
        await checks.run()
    finally:
        await checks.close()
    print(f"PASS {checks.count} end-to-end groups", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uri", default="ws://127.0.0.1:19763/ws")
    args = parser.parse_args()
    try:
        asyncio.run(main(args.uri))
    except Exception as exc:
        print(f"FAIL {type(exc).__name__}: {exc}", file=sys.stderr)
        raise
