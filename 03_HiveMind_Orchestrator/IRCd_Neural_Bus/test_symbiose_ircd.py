import asyncio
import json
import pytest
import pytest_asyncio
from symbiose_ircd import SymbioseIRCD, JUMBO_FRAME_LIMIT


@pytest_asyncio.fixture
async def ircd_server(unused_tcp_port_factory):
    unused_tcp_port = unused_tcp_port_factory()
    ircd = SymbioseIRCD()
    # Run the server as a background task
    server_task = asyncio.create_task(
        ircd.start(host="127.0.0.1", port=unused_tcp_port)
    )
    # Give it a moment to bind
    await asyncio.sleep(0.1)
    yield ircd, unused_tcp_port
    server_task.cancel()
    try:
        await server_task
    except asyncio.CancelledError:
        pass


@pytest.mark.asyncio
async def test_moe_routing_jumbo_frame(ircd_server):
    ircd, port = ircd_server

    # Create two clients: a Scout (listener) and the Oracle (sender)
    scout_reader, scout_writer = await asyncio.open_connection(
        "127.0.0.1", port, limit=JUMBO_FRAME_LIMIT
    )
    oracle_reader, oracle_writer = await asyncio.open_connection(
        "127.0.0.1", port, limit=JUMBO_FRAME_LIMIT
    )

    try:
        channel = "#hive-mind"

        # Scout joins the channel
        scout_writer.write(f"JOIN {channel}\r\n".encode("utf-8"))
        await scout_writer.drain()

        # Oracle joins the channel
        oracle_writer.write(f"JOIN {channel}\r\n".encode("utf-8"))
        await oracle_writer.drain()

        # Small delay to ensure both are registered in the channel
        await asyncio.sleep(0.1)

        # Oracle sends a massive Jumbo Frame (e.g., 2MB of tensor data)
        # 1M floats ~ 5MB in JSON, well above RFC 1459 limits but under 10MB limit
        tensor_data = [0.1] * 500000
        payload = json.dumps({"type": "tensor_sync", "data": tensor_data})
        msg_to_send = f"PRIVMSG {channel} :{payload}\r\n".encode("utf-8")

        # Sanity check the size is huge
        assert len(msg_to_send) > 512

        oracle_writer.write(msg_to_send)
        await oracle_writer.drain()

        # Scout reads the message
        # Setting a timeout just in case
        received_data = await asyncio.wait_for(
            scout_reader.readuntil(separator=b"\r\n"), timeout=5.0
        )

        received_str = received_data.decode("utf-8").strip()

        # Expected prefix: "PRIVMSG #hive-mind :"
        assert received_str.startswith(f"PRIVMSG {channel} :")

        # Extract the JSON payload
        received_payload_str = received_str[len(f"PRIVMSG {channel} :") :]
        received_payload = json.loads(received_payload_str)

        # Assert data integrity
        assert received_payload["type"] == "tensor_sync"
        assert len(received_payload["data"]) == 500000
        assert received_payload["data"][0] == 0.1

    finally:
        scout_writer.close()
        await scout_writer.wait_closed()
        oracle_writer.close()
        await oracle_writer.wait_closed()


@pytest.mark.asyncio
async def test_sender_not_disconnected_on_recipient_error(ircd_server):
    ircd, port = ircd_server

    # Create one sender and two recipients
    sender_reader, sender_writer = await asyncio.open_connection("127.0.0.1", port)
    rec1_reader, rec1_writer = await asyncio.open_connection("127.0.0.1", port)
    rec2_reader, rec2_writer = await asyncio.open_connection("127.0.0.1", port)

    try:
        channel = "#test-robustness"

        # All join the channel
        for w in [sender_writer, rec1_writer, rec2_writer]:
            w.write(f"JOIN {channel}\r\n".encode("utf-8"))
            await w.drain()

        # Wait for joins to process
        await asyncio.sleep(0.1)

        # Mock rec1's drain to raise an exception, simulating a broken connection
        for client in ircd.clients:
            if (
                client.get_extra_info("peername")[1]
                == rec1_writer.get_extra_info("sockname")[1]
            ):

                async def fake_drain():
                    raise ConnectionResetError("Mocked recipient error")

                client.drain = fake_drain
                break

        # Sender sends a message
        msg1 = f"PRIVMSG {channel} :hello\r\n".encode("utf-8")
        sender_writer.write(msg1)
        await sender_writer.drain()

        # Wait for the server to process the message and the mocked exception
        await asyncio.sleep(0.1)

        # 1. Sender should still be able to send (not crashed/disconnected)
        msg2 = f"PRIVMSG {channel} :hello2\r\n".encode("utf-8")
        sender_writer.write(msg2)
        await sender_writer.drain()
        await asyncio.sleep(0.1)

        # 2. The *other* recipient (rec2) should have received both messages
        # despite the error on rec1.
        received_data1 = await asyncio.wait_for(
            rec2_reader.readuntil(separator=b"\r\n"), timeout=2.0
        )
        assert received_data1.decode("utf-8").strip() == f"PRIVMSG {channel} :hello"

        received_data2 = await asyncio.wait_for(
            rec2_reader.readuntil(separator=b"\r\n"), timeout=2.0
        )
        assert received_data2.decode("utf-8").strip() == f"PRIVMSG {channel} :hello2"

    finally:
        for w in [sender_writer, rec1_writer, rec2_writer]:
            w.close()
            await w.wait_closed()
