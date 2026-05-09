/**
 * ═══════════════════════════════════════════════════════════════════════
 * App.tsx — SymbioseTerminal React Frontend
 *
 * UI-001: Glassmorphic layout, chat window, hardware allocation sliders.
 *
 * Architecture:
 *   - Tauri 2.0 IPC bridge to Rust backend
 *   - IRC chat relayed via src-tauri/src/irc_client.rs
 *   - Hardware telemetry via SHM ring reader
 *
 * Reference: Interactive_Plan.md §XI UI-001
 * ═══════════════════════════════════════════════════════════════════════
 */

import React, { useState, useEffect, useRef, useCallback } from 'react';

// Tauri 2.0 invoke (stubbed for build — real runtime provides this)
declare function __TAURI_INVOKE__(cmd: string, args?: Record<string, unknown>): Promise<unknown>;
const invoke = typeof __TAURI_INVOKE__ !== 'undefined' ? __TAURI_INVOKE__ : async (cmd: string, args?: Record<string, unknown>) => {
    console.log(`[STUB] invoke("${cmd}", ${JSON.stringify(args)})`);
    return null;
};

// ── Types ────────────────────────────────────────────────────────────

interface ChatMessage {
    id: string;
    sender: 'user' | 'hive' | 'system';
    content: string;
    timestamp: number;
    modality?: string; // VIDEO_FRAME, TTS_AUDIO, MOVIOLA_DELTA, etc.
}

interface HiveTelemetry {
    ram_used_mb: number;
    ram_total_mb: number;
    vcpu_count: number;
    vcpu_load: number[];
    gpu_vram_used_mb: number;
    gpu_vram_total_mb: number;
    gpu_temp_c: number;
    shm_slots_active: number;
    shm_slots_total: number;
    irc_connected: boolean;
    rdi_value: number;
    rdi_converged: boolean;
    cluster_nodes: number;
    inference_tps: number;
}

interface ScreenIdleState {
    idle: boolean;
    fps: number;
    sparsity: number;
}

// ── Main App ─────────────────────────────────────────────────────────

const App: React.FC = () => {
    const [messages, setMessages] = useState<ChatMessage[]>([]);
    const [inputText, setInputText] = useState('');
    const [telemetry, setTelemetry] = useState<HiveTelemetry | null>(null);
    const [screenIdle, setScreenIdle] = useState<ScreenIdleState>({
        idle: false, fps: 15, sparsity: 0
    });
    const [connected, setConnected] = useState(false);
    const chatEndRef = useRef<HTMLDivElement>(null);
    const inputRef = useRef<HTMLInputElement>(null);

    // Easter egg counter (UI-009)
    const [gnutellaCount, setGnutellaCount] = useState(() => {
        return parseInt(localStorage.getItem('gnutella_count') || '0', 10);
    });
    const [showGnutella, setShowGnutella] = useState(false);

    // ── Auto-scroll chat ─────────────────────────────────────────────
    useEffect(() => {
        chatEndRef.current?.scrollIntoView({ behavior: 'smooth' });
    }, [messages]);

    // ── Telemetry polling ────────────────────────────────────────────
    useEffect(() => {
        const interval = setInterval(async () => {
            try {
                const data = await invoke('get_telemetry') as HiveTelemetry | null;
                if (data) setTelemetry(data);
            } catch { /* backend not ready */ }
        }, 1000);
        return () => clearInterval(interval);
    }, []);

    // ── IRC message listener ─────────────────────────────────────────
    useEffect(() => {
        const interval = setInterval(async () => {
            try {
                const msgs = await invoke('poll_messages') as ChatMessage[] | null;
                if (msgs && msgs.length > 0) {
                    setMessages(prev => [...prev, ...msgs]);
                    setConnected(true);
                }
            } catch { /* not connected */ }
        }, 200);
        return () => clearInterval(interval);
    }, []);

    // ── UI-009: Gnutella Easter Egg (Ctrl+Shift+G) ───────────────────
    useEffect(() => {
        const handler = (e: KeyboardEvent) => {
            if (e.ctrlKey && e.shiftKey && e.key === 'G') {
                e.preventDefault();
                triggerGnutella(true);
            }
        };
        window.addEventListener('keydown', handler);
        return () => window.removeEventListener('keydown', handler);
    }, [gnutellaCount]);

    const triggerGnutella = useCallback((silent: boolean) => {
        const newCount = gnutellaCount + 1;
        setGnutellaCount(newCount);
        localStorage.setItem('gnutella_count', String(newCount));
        setShowGnutella(true);

        // Auto-dismiss after 8 seconds
        setTimeout(() => setShowGnutella(false), 8000);

        // Add IRC notice (unless silent trigger)
        if (!silent) {
            setMessages(prev => [...prev, {
                id: `gnutella-${Date.now()}`,
                sender: 'system',
                content: '✧ In memory of the network that started it all ✧',
                timestamp: Date.now()
            }]);
        }
    }, [gnutellaCount]);

    // ── Send message ─────────────────────────────────────────────────
    const sendMessage = async () => {
        const text = inputText.trim();
        if (!text) return;

        // UI-009: Easter egg trigger
        if (text === '/gnutella') {
            triggerGnutella(false);
            setInputText('');
            return;
        }

        const msg: ChatMessage = {
            id: `user-${Date.now()}`,
            sender: 'user',
            content: text,
            timestamp: Date.now()
        };

        setMessages(prev => [...prev, msg]);
        setInputText('');

        try {
            await invoke('send_chat', { message: text });
        } catch (err) {
            setMessages(prev => [...prev, {
                id: `err-${Date.now()}`,
                sender: 'system',
                content: `[ERROR] Failed to send: ${err}`,
                timestamp: Date.now()
            }]);
        }
    };

    // ── Render ────────────────────────────────────────────────────────
    return (
        <div className="app-container">
            {/* Gnutella Easter Egg Overlay (UI-009) */}
            {showGnutella && (
                <GnutellaOverlay
                    count={gnutellaCount}
                    clusterNodes={telemetry?.cluster_nodes || 0}
                />
            )}

            {/* Header */}
            <header className="app-header">
                <div className="header-left">
                    <h1 className="logo">◈ SymbioseOS</h1>
                    <span className={`status-dot ${connected ? 'connected' : 'disconnected'}`} />
                    <span className="status-text">
                        {connected ? 'Hive Active' : 'Connecting...'}
                    </span>
                </div>
                <div className="header-right">
                    {telemetry && (
                        <>
                            <TelemetryPill
                                label="TPS"
                                value={telemetry.inference_tps.toFixed(1)}
                                color="#00ff88"
                            />
                            <TelemetryPill
                                label="RDI"
                                value={telemetry.rdi_value.toFixed(3)}
                                color={telemetry.rdi_converged ? '#00ff88' : '#ffaa00'}
                            />
                            <TelemetryPill
                                label="Nodes"
                                value={String(telemetry.cluster_nodes)}
                                color="#88aaff"
                            />
                        </>
                    )}
                </div>
            </header>

            {/* Main Content */}
            <div className="main-content">
                {/* Sidebar: Hardware Telemetry */}
                <aside className="sidebar glass-panel">
                    <h2 className="sidebar-title">Hardware</h2>
                    {telemetry ? (
                        <>
                            <GaugeBar
                                label="RAM"
                                value={telemetry.ram_used_mb}
                                max={telemetry.ram_total_mb}
                                unit="MB"
                                color="#4ecdc4"
                            />
                            <GaugeBar
                                label="VRAM"
                                value={telemetry.gpu_vram_used_mb}
                                max={telemetry.gpu_vram_total_mb}
                                unit="MB"
                                color="#ff6b6b"
                            />
                            <GaugeBar
                                label="GPU"
                                value={telemetry.gpu_temp_c}
                                max={100}
                                unit="°C"
                                color={telemetry.gpu_temp_c > 80 ? '#ff4444' : '#ffaa00'}
                            />
                            <GaugeBar
                                label="SHM"
                                value={telemetry.shm_slots_active}
                                max={telemetry.shm_slots_total}
                                unit="slots"
                                color="#a855f7"
                            />

                            <div className="sidebar-section">
                                <h3>Screen Capture</h3>
                                <div className="idle-indicator">
                                    <span className={`idle-dot ${screenIdle.idle ? 'idle' : 'active'}`} />
                                    <span>{screenIdle.idle ? 'IDLE (1fps)' : `ACTIVE (${screenIdle.fps}fps)`}</span>
                                </div>
                                <div className="sparsity-bar">
                                    <span>Sparsity: {(screenIdle.sparsity * 100).toFixed(1)}%</span>
                                </div>
                            </div>

                            <div className="sidebar-section">
                                <h3>vCPU Load</h3>
                                <div className="vcpu-grid">
                                    {telemetry.vcpu_load.map((load, i) => (
                                        <div
                                            key={i}
                                            className="vcpu-cell"
                                            style={{
                                                backgroundColor: `hsl(${120 - load * 1.2}, 80%, ${30 + load * 0.2}%)`
                                            }}
                                            title={`vCPU ${i}: ${load.toFixed(0)}%`}
                                        />
                                    ))}
                                </div>
                            </div>
                        </>
                    ) : (
                        <div className="loading-skeleton">
                            <div className="skeleton-bar" />
                            <div className="skeleton-bar" />
                            <div className="skeleton-bar" />
                        </div>
                    )}
                </aside>

                {/* Chat Window */}
                <main className="chat-container glass-panel">
                    <div className="chat-messages">
                        {messages.map(msg => (
                            <ChatBubble key={msg.id} message={msg} />
                        ))}
                        <div ref={chatEndRef} />
                    </div>

                    <div className="chat-input-row">
                        <input
                            ref={inputRef}
                            type="text"
                            className="chat-input"
                            value={inputText}
                            onChange={e => setInputText(e.target.value)}
                            onKeyDown={e => e.key === 'Enter' && sendMessage()}
                            placeholder="Message the hive mind..."
                            autoFocus
                        />
                        <button className="send-btn" onClick={sendMessage}>
                            ▶
                        </button>
                    </div>
                </main>
            </div>
        </div>
    );
};

// ── Sub-Components ───────────────────────────────────────────────────

const ChatBubble: React.FC<{ message: ChatMessage }> = ({ message }) => {
    const time = new Date(message.timestamp).toLocaleTimeString([], {
        hour: '2-digit', minute: '2-digit', second: '2-digit'
    });

    return (
        <div className={`chat-bubble ${message.sender}`}>
            <div className="bubble-header">
                <span className="bubble-sender">
                    {message.sender === 'user' ? 'You' :
                     message.sender === 'hive' ? '◈ Hive Mind' :
                     '⚙ System'}
                </span>
                {message.modality && (
                    <span className="bubble-modality">{message.modality}</span>
                )}
                <span className="bubble-time">{time}</span>
            </div>
            <div className="bubble-content">{message.content}</div>
        </div>
    );
};

const TelemetryPill: React.FC<{
    label: string; value: string; color: string;
}> = ({ label, value, color }) => (
    <div className="telemetry-pill" style={{ borderColor: color }}>
        <span className="pill-label">{label}</span>
        <span className="pill-value" style={{ color }}>{value}</span>
    </div>
);

const GaugeBar: React.FC<{
    label: string; value: number; max: number; unit: string; color: string;
}> = ({ label, value, max, unit, color }) => {
    const pct = Math.min(100, (value / max) * 100);
    return (
        <div className="gauge-bar">
            <div className="gauge-label">
                <span>{label}</span>
                <span>{value.toFixed(0)}/{max.toFixed(0)} {unit}</span>
            </div>
            <div className="gauge-track">
                <div
                    className="gauge-fill"
                    style={{ width: `${pct}%`, backgroundColor: color }}
                />
            </div>
        </div>
    );
};

// ── UI-009: Gnutella Easter Egg Overlay ──────────────────────────────

const GnutellaOverlay: React.FC<{
    count: number; clusterNodes: number;
}> = ({ count, clusterNodes }) => {
    const canvasRef = useRef<HTMLCanvasElement>(null);

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;
        const ctx = canvas.getContext('2d');
        if (!ctx) return;

        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;

        // Generate P2P network graph nodes
        const nodeLabels = ['JOE', 'SAIMONO'];
        for (let i = 1; i <= Math.max(3, clusterNodes); i++) {
            nodeLabels.push(`HIVE-${String(i).padStart(2, '0')}`);
        }

        const nodes = nodeLabels.map((label, i) => ({
            label,
            x: Math.random() * canvas.width * 0.8 + canvas.width * 0.1,
            y: Math.random() * canvas.height * 0.7 + canvas.height * 0.15,
            vx: (Math.random() - 0.5) * 0.3,
            vy: (Math.random() - 0.5) * 0.3,
            pulse: Math.random() * Math.PI * 2
        }));

        let alpha = 0;
        let frame = 0;
        const startTime = Date.now();

        const animate = () => {
            const elapsed = Date.now() - startTime;
            frame++;

            // Fade in (0-1s), hold (1-7s), fade out (7-8s)
            if (elapsed < 1000) alpha = elapsed / 1000;
            else if (elapsed < 7000) alpha = 1;
            else if (elapsed < 8000) alpha = 1 - (elapsed - 7000) / 1000;
            else return; // Animation complete

            ctx.clearRect(0, 0, canvas.width, canvas.height);

            // Background
            ctx.fillStyle = `rgba(26, 0, 51, ${alpha * 0.92})`;
            ctx.fillRect(0, 0, canvas.width, canvas.height);

            // Stars
            for (let i = 0; i < 80; i++) {
                const sx = (i * 137.508 + frame * 0.01) % canvas.width;
                const sy = (i * 97.337 + frame * 0.005) % canvas.height;
                const brightness = 0.3 + 0.7 * Math.sin(frame * 0.02 + i);
                ctx.fillStyle = `rgba(255, 255, 255, ${alpha * brightness * 0.5})`;
                ctx.fillRect(sx, sy, 1.5, 1.5);
            }

            // Update node positions (slow drift)
            nodes.forEach(n => {
                n.x += n.vx;
                n.y += n.vy;
                n.pulse += 0.03;
                if (n.x < 50 || n.x > canvas.width - 50) n.vx *= -1;
                if (n.y < 50 || n.y > canvas.height - 50) n.vy *= -1;
            });

            // Draw edges (Gnutella topology style)
            for (let i = 0; i < nodes.length; i++) {
                for (let j = i + 1; j < nodes.length; j++) {
                    const dx = nodes[i].x - nodes[j].x;
                    const dy = nodes[i].y - nodes[j].y;
                    const dist = Math.sqrt(dx * dx + dy * dy);
                    if (dist < 300) {
                        ctx.strokeStyle = `rgba(0, 255, 65, ${alpha * (1 - dist / 300) * 0.4})`;
                        ctx.lineWidth = 1;
                        ctx.beginPath();
                        ctx.moveTo(nodes[i].x, nodes[i].y);
                        ctx.lineTo(nodes[j].x, nodes[j].y);
                        ctx.stroke();
                    }
                }
            }

            // Draw nodes
            nodes.forEach(n => {
                const pulseR = 6 + Math.sin(n.pulse) * 2;

                // Glow
                const grd = ctx.createRadialGradient(n.x, n.y, 0, n.x, n.y, pulseR * 3);
                grd.addColorStop(0, `rgba(0, 255, 65, ${alpha * 0.3})`);
                grd.addColorStop(1, 'transparent');
                ctx.fillStyle = grd;
                ctx.fillRect(n.x - pulseR * 3, n.y - pulseR * 3, pulseR * 6, pulseR * 6);

                // Node circle
                ctx.fillStyle = `rgba(0, 255, 65, ${alpha * 0.9})`;
                ctx.beginPath();
                ctx.arc(n.x, n.y, pulseR, 0, Math.PI * 2);
                ctx.fill();

                // Label
                ctx.font = '11px monospace';
                ctx.fillStyle = `rgba(0, 255, 65, ${alpha * 0.8})`;
                ctx.textAlign = 'center';
                ctx.fillText(n.label, n.x, n.y + pulseR + 14);
            });

            // Banner text (centered)
            if (elapsed > 500) {
                const textAlpha = Math.min(1, (elapsed - 500) / 1000) * alpha;
                ctx.textAlign = 'center';

                ctx.font = 'bold 36px monospace';
                ctx.fillStyle = `rgba(0, 255, 65, ${textAlpha})`;
                ctx.fillText('« GNUTELLA LIVES »', canvas.width / 2, canvas.height / 2 - 30);

                ctx.font = '14px monospace';
                ctx.fillStyle = `rgba(200, 200, 200, ${textAlpha * 0.8})`;
                ctx.fillText(
                    'Dedicated to JOE — Master of Freedom.',
                    canvas.width / 2, canvas.height / 2 + 10
                );
                ctx.fillText(
                    'The original peer-to-peer rebels.',
                    canvas.width / 2, canvas.height / 2 + 30
                );
                ctx.fillText(
                    'From Gnutella to SymbioseOS, decentralization is in our blood.',
                    canvas.width / 2, canvas.height / 2 + 50
                );

                // 10th activation bonus (UI-009 spec)
                if (count >= 10) {
                    ctx.font = 'italic 13px monospace';
                    ctx.fillStyle = `rgba(168, 85, 247, ${textAlpha * 0.9})`;
                    ctx.fillText(
                        '"The first thing we ever decentralized was our minds."',
                        canvas.width / 2, canvas.height / 2 + 80
                    );
                }
            }

            requestAnimationFrame(animate);
        };

        requestAnimationFrame(animate);
    }, [count, clusterNodes]);

    return (
        <canvas
            ref={canvasRef}
            className="gnutella-overlay"
            style={{
                position: 'fixed', top: 0, left: 0,
                width: '100vw', height: '100vh',
                zIndex: 9999, pointerEvents: 'none'
            }}
        />
    );
};

export default App;
