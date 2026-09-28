"""Robot seat checks against a disposable local online service."""

import argparse
import asyncio
import ipaddress
import time
from urllib.parse import urlsplit

from online_formal_test import Checks, ProtocolError


async def run(uri):
    parts = urlsplit(uri)
    try:
        loopback = ipaddress.ip_address(parts.hostname).is_loopback
    except (ValueError, TypeError):
        loopback = parts.hostname and parts.hostname.lower() == "localhost"
    if parts.scheme not in {"ws", "wss"} or not loopback:
        raise SystemExit("Only a loopback WebSocket service is accepted")

    checks = Checks(uri)
    try:
        observer = await checks.register("bot_observer", "旁观者")
        for count in (2, 3, 4):
            rooms = (await observer.request("list", payload={"player_count": count}))[
                "payload"]["rooms"]
            room = next(item["room_id"] for item in rooms
                        if item["occupied"] == 0 and item["status"] == "waiting")
            host = await checks.register(f"bot_host_{count}", f"房主{count}")
            await checks.action(host, "join", room)
            await observer.error("invite_bots", "not_seated", room=room,
                                 seq=await checks.room_seq(observer, room))
            filled = await checks.action(host, "invite_bots", room)
            view = filled["payload"]["view"]
            assert view["occupied"] == count
            assert [seat["nickname"] for seat in view["seats"][1:]] == [
                f"机器人 {number}" for number in range(1, count)]
            checks.private_view(view)
            await observer.error("join", "room_full", room=room,
                                 seq=await checks.room_seq(observer, room))
            started = await checks.action(host, "start", room)
            assert started["payload"]["view"]["record_eligible"] is False
            baseline = int(started["seq"])
            state = (await checks.state(host, room))["payload"]["view"]
            if int(state["seq"]) == baseline and state["current_player"] == 0:
                assert state["phase"] == 2
                bid = await checks.action(host, "bid", room, {"score": 0})
                baseline = int(bid["seq"])
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                state = (await checks.state(host, room))["payload"]["view"]
                if int(state["seq"]) > baseline:
                    break
                await asyncio.sleep(0.1)
            else:
                raise AssertionError(f"{room}: invited bot did not act")
            for _ in range(5):
                try:
                    await checks.action(host, "leave", room)
                    break
                except ProtocolError as exc:
                    if exc.code != "stale_seq":
                        raise
            else:
                raise AssertionError(f"{room}: host could not leave")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                rooms = (await observer.request("list", payload={"player_count": count}))[
                    "payload"]["rooms"]
                summary = next(item for item in rooms if item["room_id"] == room)
                if summary["occupied"] == 0 and summary["status"] == "waiting":
                    break
                await asyncio.sleep(0.1)
            else:
                raise AssertionError(f"{room}: bot seats remained after host left")
            checks.passed(f"{count} player robot fill, turn and cleanup")

        rooms = (await observer.request("list", payload={"player_count": 4}))[
            "payload"]["rooms"]
        room = next(item["room_id"] for item in rooms
                    if item["occupied"] == 0 and item["status"] == "waiting")
        host = await checks.register("mixed_host", "混合房主")
        guest = await checks.register("mixed_guest", "混合玩家")
        await checks.action(host, "join", room)
        await checks.action(guest, "join", room)
        await guest.error("invite_bots", "host_required", room=room,
                          seq=await checks.room_seq(guest, room))
        filled = await checks.action(host, "invite_bots", room)
        assert [seat["nickname"] for seat in filled["payload"]["view"]["seats"]] == [
            "混合房主", "混合玩家", "机器人 1", "机器人 2"]
        await checks.action(host, "leave", room)
        remaining = (await checks.state(guest, room))["payload"]["view"]
        assert remaining["is_host"] and remaining["occupied"] == 3
        assert [seat["nickname"] for seat in remaining["seats"][:3]] == [
            "混合玩家", "机器人 1", "机器人 2"]
        await checks.action(guest, "leave", room)
        summary = next(item for item in (await observer.request(
            "list", payload={"player_count": 4}))["payload"]["rooms"]
            if item["room_id"] == room)
        assert summary["occupied"] == 0
        checks.passed("mixed human seats, host succession and robot cleanup")

        rooms = (await observer.request("list", payload={"player_count": 4}))[
            "payload"]["rooms"]
        room = next(item["room_id"] for item in rooms
                    if item["occupied"] == 0 and item["status"] == "waiting")
        trio = [await checks.register(f"trio_{index}", f"真人{index}")
                for index in range(1, 4)]
        for member in trio:
            await checks.action(member, "join", room)
        filled = await checks.action(trio[0], "invite_bots", room)
        assert [seat["nickname"] for seat in filled["payload"]["view"]["seats"]] == [
            "真人1", "真人2", "真人3", "机器人 1"]
        started = await checks.action(trio[0], "start", room)
        assert started["payload"]["view"]["record_eligible"] is False
        checks.passed("three humans and one robot can start a four-player round")

        rooms = (await observer.request("list", payload={"player_count": 2}))[
            "payload"]["rooms"]
        room = next(item["room_id"] for item in rooms
                    if item["occupied"] == 0 and item["status"] == "waiting")
        solo = await checks.register("play_host", "单人玩家")
        await checks.action(solo, "join", room)
        await checks.action(solo, "invite_bots", room)
        await checks.action(solo, "start", room)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            state = (await checks.state(solo, room))["payload"]["view"]
            if state["status"] == "playing" and state["phase"] == 2:
                if state["current_player"] == 0:
                    await checks.action(solo, "bid", room, {"score": 3})
            elif state["status"] == "playing" and state["phase"] == 4:
                if state["current_player"] == 0:
                    leading = (not state["last_played_cards"] or
                               state["last_played_by"] == 0)
                    if leading:
                        reply = await checks.action(solo, "play", room,
                                                    {"card_ids": [state["hand"][0]]})
                    else:
                        reply = await checks.action(solo, "pass", room)
                    after_human = int(reply["seq"])
                    while time.monotonic() < deadline:
                        state = (await checks.state(solo, room))["payload"]["view"]
                        if int(state["seq"]) > after_human:
                            break
                        await asyncio.sleep(0.1)
                    else:
                        raise AssertionError("robot did not play after the human turn")
                    break
            await asyncio.sleep(0.1)
        else:
            raise AssertionError("two-player game did not reach a robot play turn")
        for _ in range(5):
            try:
                await checks.action(solo, "leave", room)
                break
            except ProtocolError as exc:
                if exc.code != "stale_seq":
                    raise
        checks.passed("robot play action after a human card turn")
    finally:
        await checks.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uri", default="ws://127.0.0.1:19763/ws")
    asyncio.run(run(parser.parse_args().uri))
