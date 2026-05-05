import asyncio
import logging
from typing import Dict, Set

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("SymbioseIRCD")

# 10MB Jumbo Frame limit to bypass RFC 1459 512-byte limit
JUMBO_FRAME_LIMIT = 10 * 1024 * 1024


class SymbioseIRCD:
    def __init__(self) -> None:
        self.clients: Set[asyncio.StreamWriter] = set()
        self.channels: Dict[str, Set[asyncio.StreamWriter]] = {
            "#recon": set(),
            "#math": set(),
            "#hive-mind": set(),
        }

    async def handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        self.clients.add(writer)
        client_addr = writer.get_extra_info("peername")
        logger.info("Scout connected: %s", client_addr)

        try:
            while True:
                # Read up to Jumbo Frame limit
                data = await reader.readuntil(separator=b"\r\n")
                if not data:
                    break

                message = data.decode("utf-8").strip()
                if not message:
                    continue

                await self.process_message(writer, message)
        except asyncio.exceptions.IncompleteReadError:
            pass
        except asyncio.exceptions.LimitOverrunError:
            logger.warning("Message from %s exceeded Jumbo Frame limit!", client_addr)
        except Exception as e:
            logger.error("Error handling client %s: %s", client_addr, e)
        finally:
            self.clients.remove(writer)
            for channel_clients in self.channels.values():
                if writer in channel_clients:
                    channel_clients.remove(writer)
            writer.close()
            await writer.wait_closed()
            logger.info("Scout disconnected: %s", client_addr)

    async def process_message(self, sender: asyncio.StreamWriter, message: str) -> None:
        parts = message.split(" ", 2)
        if not parts:
            return

        command = parts[0].upper()

        if command == "JOIN":
            if len(parts) > 1:
                channel = parts[1]
                if channel not in self.channels:
                    self.channels[channel] = set()
                self.channels[channel].add(sender)
                logger.info("Scout joined %s", channel)

        elif command == "PRIVMSG":
            if len(parts) > 2:
                target = parts[1]
                content = parts[2]

                if target in self.channels:
                    # Broadcast to everyone in channel except sender
                    payload = f"PRIVMSG {target} {content}\r\n".encode("utf-8")
                    for client in list(self.channels[target]):
                        if client != sender:
                            client.write(payload)
                            await client.drain()

    async def start(self, host: str = "127.0.0.1", port: int = 6667) -> None:
        server = await asyncio.start_server(
            self.handle_client, host, port, limit=JUMBO_FRAME_LIMIT
        )
        if server.sockets:
            addrs = ", ".join(str(sock.getsockname()) for sock in server.sockets)
            logger.info(
                "Serving Symbiose IRCd on %s with Jumbo Frames (%d bytes)",
                addrs,
                JUMBO_FRAME_LIMIT,
            )

        async with server:
            await server.serve_forever()


if __name__ == "__main__":
    ircd = SymbioseIRCD()
    asyncio.run(ircd.start())
