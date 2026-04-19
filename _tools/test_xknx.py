#!/usr/bin/env python3
"""
XKNX reliability test for ESP32 KNX IP Secure server.
Tests connect/disconnect cycles and basic tunnelling.
Usage: python3 test_xknx.py [--host HOST] [--cycles N]
"""
import asyncio
import argparse
import sys
import traceback

# pip install xknx
from xknx import XKNX
from xknx.io import ConnectionConfig, ConnectionType, SecureConfig
from xknx.dpt import DPTBinary
from xknx.telegram import GroupAddress, Telegram
from xknx.telegram.apci import GroupValueWrite, GroupValueRead


async def test_connect_disconnect(host, password, cycles=5):
    """Test multiple connect/disconnect cycles."""
    print(f"=== Connect/Disconnect test: {cycles} cycles to {host} ===")
    for i in range(1, cycles + 1):
        try:
            xknx = XKNX(
                connection_config=ConnectionConfig(
                    connection_type=ConnectionType.TUNNELING_TCP_SECURE,
                    gateway_ip=host,
                    gateway_port=3671,
                    secure_config=SecureConfig(
                        user_id=2,
                        user_password=password,
                    ),
                )
            )
            await xknx.start()
            print(f"  [{i}/{cycles}] Connected OK (state={xknx.connected.is_set()})")
            await asyncio.sleep(0.5)
            await xknx.stop()
            print(f"  [{i}/{cycles}] Disconnected OK")
            await asyncio.sleep(0.5)
        except Exception as e:
            print(f"  [{i}/{cycles}] FAILED: {e}")
            traceback.print_exc()
            return False
    print(f"=== All {cycles} connect/disconnect cycles PASSED ===\n")
    return True


async def test_group_write(host, password):
    """Test sending a group write telegram."""
    print(f"=== Group Write test to {host} ===")
    try:
        xknx = XKNX(
            connection_config=ConnectionConfig(
                connection_type=ConnectionType.TUNNELING_TCP_SECURE,
                gateway_ip=host,
                gateway_port=3671,
                secure_config=SecureConfig(
                    user_id=2,
                    user_password=password,
                ),
            )
        )
        await xknx.start()
        if not xknx.connected.is_set():
            print("  FAILED: not connected")
            return False

        telegram = Telegram(
            destination_address=GroupAddress("0/0/1"),
            payload=GroupValueWrite(DPTBinary(1)),
        )
        await xknx.telegrams.put(telegram)
        await asyncio.sleep(1)
        print("  Group write 0/0/1 = 1 sent OK")

        await xknx.stop()
        print("=== Group Write test PASSED ===\n")
        return True
    except Exception as e:
        print(f"  FAILED: {e}")
        traceback.print_exc()
        return False


async def test_sustained_connection(host, password, duration=10):
    """Test keeping a connection open for a period."""
    print(f"=== Sustained connection test ({duration}s) to {host} ===")
    try:
        xknx = XKNX(
            connection_config=ConnectionConfig(
                connection_type=ConnectionType.TUNNELING_TCP_SECURE,
                gateway_ip=host,
                gateway_port=3671,
                secure_config=SecureConfig(
                    user_id=2,
                    user_password=password,
                ),
            )
        )
        await xknx.start()
        if not xknx.connected.is_set():
            print("  FAILED: not connected")
            return False

        for s in range(duration):
            if not xknx.connected.is_set():
                print(f"  FAILED: disconnected after {s}s")
                return False
            await asyncio.sleep(1)
        print(f"  Connection held for {duration}s OK")

        await xknx.stop()
        print("=== Sustained connection test PASSED ===\n")
        return True
    except Exception as e:
        print(f"  FAILED: {e}")
        traceback.print_exc()
        return False


async def test_concurrent_connections(host, password, count=2):
    """Test multiple simultaneous connections."""
    print(f"=== Concurrent connections test ({count}) to {host} ===")
    xknxs = []
    try:
        for i in range(count):
            x = XKNX(
                connection_config=ConnectionConfig(
                    connection_type=ConnectionType.TUNNELING_TCP_SECURE,
                    gateway_ip=host,
                    gateway_port=3671,
                    secure_config=SecureConfig(
                        user_id=2,
                        user_password=password,
                    ),
                )
            )
            await x.start()
            if not x.connected.is_set():
                print(f"  FAILED: connection {i+1} not connected")
                return False
            xknxs.append(x)
            print(f"  Connection {i+1}/{count} established")

        await asyncio.sleep(2)
        all_connected = all(x.connected.is_set() for x in xknxs)
        print(f"  All still connected: {all_connected}")

        for i, x in enumerate(xknxs):
            await x.stop()
            print(f"  Connection {i+1}/{count} closed")

        print(f"=== Concurrent connections test {'PASSED' if all_connected else 'FAILED'} ===\n")
        return all_connected
    except Exception as e:
        print(f"  FAILED: {e}")
        traceback.print_exc()
        for x in xknxs:
            try:
                await x.stop()
            except:
                pass
        return False


async def main():
    parser = argparse.ArgumentParser(description="XKNX reliability test")
    parser.add_argument("--host", default="192.168.1.112", help="ESP32 IP")
    parser.add_argument("--password", default="secret", help="User password")
    parser.add_argument("--cycles", type=int, default=5, help="Connect/disconnect cycles")
    parser.add_argument("--duration", type=int, default=10, help="Sustained connection seconds")
    parser.add_argument("--concurrent", type=int, default=2, help="Concurrent connections")
    args = parser.parse_args()

    results = {}
    results["connect_disconnect"] = await test_connect_disconnect(args.host, args.password, args.cycles)
    results["group_write"] = await test_group_write(args.host, args.password)
    results["sustained"] = await test_sustained_connection(args.host, args.password, args.duration)
    results["concurrent"] = await test_concurrent_connections(args.host, args.password, args.concurrent)

    print("=" * 50)
    print("RESULTS:")
    all_pass = True
    for name, passed in results.items():
        status = "PASS" if passed else "FAIL"
        print(f"  {name}: {status}")
        if not passed:
            all_pass = False
    print("=" * 50)
    print(f"Overall: {'ALL PASSED' if all_pass else 'SOME FAILED'}")
    sys.exit(0 if all_pass else 1)


if __name__ == "__main__":
    asyncio.run(main())
