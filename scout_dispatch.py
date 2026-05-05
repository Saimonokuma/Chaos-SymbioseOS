import asyncio
import json
import logging
import sys

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("ScoutDispatch")

# Jumbo Frame limit matching the IRCd
JUMBO_FRAME_LIMIT = 10 * 1024 * 1024


async def dispatch_to_scout(channel: str, task: dict) -> None:
    reader, writer = await asyncio.open_connection(
        "127.0.0.1", 6667, limit=JUMBO_FRAME_LIMIT
    )

    try:
        # Join the required channel
        join_msg = f"JOIN {channel}\r\n".encode("utf-8")
        writer.write(join_msg)
        await writer.drain()

        # Dispatch the payload
        payload = json.dumps(task)
        # We prefix the JSON with a colon as per basic IRC PRIVMSG formatting
        msg = f"PRIVMSG {channel} :{payload}\r\n".encode("utf-8")

        writer.write(msg)
        await writer.drain()

        logger.info(f"Dispatched task to {channel} (Payload size: {len(msg)} bytes)")

        # Read acknowledgment or response
        # Using a timeout just for demonstration purposes
        try:
            response = await asyncio.wait_for(
                reader.readuntil(separator=b"\r\n"), timeout=2.0
            )
            logger.info(f"Received from Hive Mind: {response.decode('utf-8').strip()}")
        except asyncio.TimeoutError:
            logger.info("No immediate response from Hive Mind.")

    finally:
        writer.close()
        await writer.wait_closed()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--jumbo":
        # Generate a massive payload to test Jumbo Frames
        massive_tensor = [0.0] * 1000000  # ~1 million floats
        task_data = {"type": "tensor_migration", "data": massive_tensor}
        asyncio.run(dispatch_to_scout("#hive-mind", task_data))
    else:
        # Normal task
        task_data = {"type": "recon", "target": "https://example.com", "depth": "2"}
        asyncio.run(dispatch_to_scout("#recon", task_data))
