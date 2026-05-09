// ═══════════════════════════════════════════════════════════════════════
// irc_client.rs — UI-004: Rust IRC Client for SymbioseTerminal
//
// Connects to localhost:6667, authenticates as Host Client, relays
// chat messages to/from the LLM via #oracle channel.
//
// Also handles UI-009 easter egg IRC NOTICE.
//
// Reference: Interactive_Plan.md §XI UI-004, §VII·2
// ═══════════════════════════════════════════════════════════════════════

use std::io::{BufRead, BufReader, Write};
use std::net::TcpStream;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

/// IRC message parsed from raw line
#[derive(Debug, Clone)]
pub struct IrcMessage {
    /// Source (nick!user@host)
    pub prefix: Option<String>,
    /// IRC command (PRIVMSG, NOTICE, JOIN, etc.)
    pub command: String,
    /// Target channel or nick
    pub target: String,
    /// Message content
    pub content: String,
    /// Modality tag if present (VIDEO_FRAME, TTS_AUDIO, etc.)
    pub modality: Option<String>,
    /// SHM slot offset if present
    pub shm_offset: Option<u64>,
}

/// IRC Client handle
pub struct IrcClient {
    /// TCP connection to IRCD
    stream: Option<TcpStream>,
    /// Thread-safe message queue (received from server)
    inbox: Arc<Mutex<Vec<IrcMessage>>>,
    /// Running flag
    running: Arc<AtomicBool>,
    /// Our nickname
    nick: String,
    /// Connection state
    connected: Arc<AtomicBool>,
}

impl IrcClient {
    /// Create a new IRC client (not yet connected).
    pub fn new(nick: &str) -> Self {
        Self {
            stream: None,
            inbox: Arc::new(Mutex::new(Vec::new())),
            running: Arc::new(AtomicBool::new(false)),
            nick: nick.to_string(),
            connected: Arc::new(AtomicBool::new(false)),
        }
    }

    /// Connect to localhost:6667 and authenticate.
    ///
    /// Sends NICK/USER, waits for 001 (RPL_WELCOME), joins #oracle.
    pub fn connect(&mut self) -> Result<(), String> {
        let stream = TcpStream::connect("127.0.0.1:6667")
            .map_err(|e| format!("Connection failed: {}", e))?;

        stream
            .set_nonblocking(false)
            .map_err(|e| format!("Set blocking failed: {}", e))?;

        stream
            .set_read_timeout(Some(std::time::Duration::from_secs(30)))
            .ok();

        let mut writer = stream.try_clone()
            .map_err(|e| format!("Clone failed: {}", e))?;

        // Send registration
        writeln!(writer, "NICK {}", self.nick)
            .map_err(|e| format!("NICK failed: {}", e))?;
        writeln!(writer, "USER {} 0 * :SymbioseOS Terminal Client", self.nick)
            .map_err(|e| format!("USER failed: {}", e))?;
        writer.flush().map_err(|e| format!("Flush failed: {}", e))?;

        // Wait for RPL_WELCOME (001)
        let reader = BufReader::new(stream.try_clone()
            .map_err(|e| format!("Clone failed: {}", e))?);
        let mut registered = false;

        for line in reader.lines().take(50) {
            match line {
                Ok(raw) => {
                    if raw.contains(" 001 ") {
                        registered = true;
                        break;
                    }
                    // Respond to PING during registration
                    if raw.starts_with("PING") {
                        let pong = raw.replacen("PING", "PONG", 1);
                        writeln!(writer, "{}", pong).ok();
                        writer.flush().ok();
                    }
                }
                Err(_) => break,
            }
        }

        if !registered {
            return Err("Registration timeout — no RPL_WELCOME received".into());
        }

        // Join #oracle channel
        writeln!(writer, "JOIN #oracle")
            .map_err(|e| format!("JOIN failed: {}", e))?;
        writer.flush().map_err(|e| format!("Flush failed: {}", e))?;

        // Also join #telemetry for SCREEN_IDLE/SCREEN_ACTIVE (UI-008)
        writeln!(writer, "JOIN #telemetry")
            .map_err(|e| format!("JOIN failed: {}", e))?;
        writer.flush().map_err(|e| format!("Flush failed: {}", e))?;

        self.stream = Some(stream.try_clone()
            .map_err(|e| format!("Clone failed: {}", e))?);
        self.connected.store(true, Ordering::Release);
        self.running.store(true, Ordering::Release);

        // Start reader thread
        let inbox = self.inbox.clone();
        let running = self.running.clone();
        let connected = self.connected.clone();
        let mut ping_writer = stream.try_clone()
            .map_err(|e| format!("Clone failed: {}", e))?;

        std::thread::spawn(move || {
            let reader = BufReader::new(stream);

            for line in reader.lines() {
                if !running.load(Ordering::Acquire) {
                    break;
                }

                match line {
                    Ok(raw) => {
                        // Handle PING/PONG keepalive
                        if raw.starts_with("PING") {
                            let pong = raw.replacen("PING", "PONG", 1);
                            writeln!(ping_writer, "{}", pong).ok();
                            ping_writer.flush().ok();
                            continue;
                        }

                        // Parse IRC message
                        if let Some(msg) = parse_irc_line(&raw) {
                            inbox.lock().unwrap().push(msg);
                        }
                    }
                    Err(_) => {
                        connected.store(false, Ordering::Release);
                        break;
                    }
                }
            }

            running.store(false, Ordering::Release);
            connected.store(false, Ordering::Release);
        });

        Ok(())
    }

    /// Send a PRIVMSG to #oracle.
    pub fn send_chat(&mut self, message: &str) -> Result<(), String> {
        let stream = self.stream.as_mut()
            .ok_or("Not connected")?;
        writeln!(stream, "PRIVMSG #oracle :{}", message)
            .map_err(|e| format!("Send failed: {}", e))?;
        stream.flush().map_err(|e| format!("Flush failed: {}", e))?;
        Ok(())
    }

    /// Send a tagged modality message (e.g., VIDEO_FRAME with SHM offset).
    pub fn send_modality(
        &mut self,
        channel: &str,
        modality_type: &str,
        shm_offset: u64,
        payload_len: u64,
    ) -> Result<(), String> {
        let stream = self.stream.as_mut()
            .ok_or("Not connected")?;
        writeln!(
            stream,
            "PRIVMSG {} :{} shm_offset={} len={}",
            channel, modality_type, shm_offset, payload_len
        )
            .map_err(|e| format!("Send failed: {}", e))?;
        stream.flush().map_err(|e| format!("Flush failed: {}", e))?;
        Ok(())
    }

    /// Send a NOTICE (for system messages like easter egg).
    pub fn send_notice(&mut self, target: &str, message: &str) -> Result<(), String> {
        let stream = self.stream.as_mut()
            .ok_or("Not connected")?;
        writeln!(stream, "NOTICE {} :{}", target, message)
            .map_err(|e| format!("Send failed: {}", e))?;
        stream.flush().map_err(|e| format!("Flush failed: {}", e))?;
        Ok(())
    }

    /// Send screen idle/active transitions to #telemetry (UI-008).
    pub fn send_screen_state(&mut self, idle: bool) -> Result<(), String> {
        let msg = if idle { "SCREEN_IDLE" } else { "SCREEN_ACTIVE" };
        self.send_modality("#telemetry", msg, 0, 0)
    }

    /// Drain received messages (non-blocking).
    pub fn poll_messages(&self) -> Vec<IrcMessage> {
        let mut inbox = self.inbox.lock().unwrap();
        let messages = inbox.drain(..).collect();
        messages
    }

    /// Check connection status.
    pub fn is_connected(&self) -> bool {
        self.connected.load(Ordering::Relaxed)
    }

    /// Disconnect gracefully.
    pub fn disconnect(&mut self) {
        self.running.store(false, Ordering::Release);
        if let Some(ref mut stream) = self.stream {
            writeln!(stream, "QUIT :SymbioseOS Terminal shutting down").ok();
            stream.flush().ok();
        }
        self.stream = None;
        self.connected.store(false, Ordering::Release);
    }
}

/// Parse a raw IRC line into an IrcMessage.
fn parse_irc_line(raw: &str) -> Option<IrcMessage> {
    let raw = raw.trim();
    if raw.is_empty() {
        return None;
    }

    let (prefix, rest) = if raw.starts_with(':') {
        let space = raw.find(' ')?;
        (Some(raw[1..space].to_string()), &raw[space + 1..])
    } else {
        (None, raw)
    };

    let parts: Vec<&str> = rest.splitn(3, ' ').collect();
    if parts.len() < 2 {
        return None;
    }

    let command = parts[0].to_string();
    let target = parts[1].to_string();

    // Only process PRIVMSG and NOTICE
    if command != "PRIVMSG" && command != "NOTICE" {
        return None;
    }

    let content = if parts.len() > 2 {
        let c = parts[2];
        if c.starts_with(':') { &c[1..] } else { c }.to_string()
    } else {
        String::new()
    };

    // Extract modality tag and SHM offset from content
    let modality = extract_modality(&content);
    let shm_offset = extract_shm_offset(&content);

    Some(IrcMessage {
        prefix,
        command,
        target,
        content,
        modality,
        shm_offset,
    })
}

/// Extract modality type from message content.
fn extract_modality(content: &str) -> Option<String> {
    let modalities = [
        "VIDEO_FRAME", "SCREEN_CAP", "TTS_AUDIO", "MOVIOLA_DELTA",
        "SCREEN_IDLE", "SCREEN_ACTIVE", "MODALITY_EVOLVED",
        "SHARD_ASSIGN", "NODE_JOIN", "NODE_LEAVE",
    ];

    for m in &modalities {
        if content.starts_with(m) {
            return Some(m.to_string());
        }
    }
    None
}

/// Extract SHM offset from message content.
fn extract_shm_offset(content: &str) -> Option<u64> {
    for part in content.split_whitespace() {
        if let Some(val) = part.strip_prefix("shm_offset=") {
            return val.parse().ok();
        }
    }
    None
}

impl Drop for IrcClient {
    fn drop(&mut self) {
        self.disconnect();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_privmsg() {
        let msg = parse_irc_line(
            ":HiveMind!hive@localhost PRIVMSG #oracle :Hello from the hive"
        ).unwrap();
        assert_eq!(msg.command, "PRIVMSG");
        assert_eq!(msg.target, "#oracle");
        assert_eq!(msg.content, "Hello from the hive");
        assert!(msg.modality.is_none());
    }

    #[test]
    fn test_parse_modality_msg() {
        let msg = parse_irc_line(
            ":Terminal!host@localhost PRIVMSG #oracle :VIDEO_FRAME shm_offset=536870912 len=307200"
        ).unwrap();
        assert_eq!(msg.modality, Some("VIDEO_FRAME".to_string()));
        assert_eq!(msg.shm_offset, Some(536870912));
    }

    #[test]
    fn test_parse_ping() {
        // PING should return None (not a message)
        assert!(parse_irc_line("PING :server").is_none());
    }
}
