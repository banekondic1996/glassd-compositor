// labwc-client.js - Node.js module for NW.js to communicate with labwc
const net = require('net');
const EventEmitter = require('events');

class LabwcClient extends EventEmitter {
    constructor(socketPath = '/tmp/labwc-nwjs.sock') {
        super();
        this.socketPath = socketPath;
        this.socket = null;
        this.reconnectTimer = null;
        this.windows = new Map();
        this.cursorX = 0;
        this.cursorY = 0;
        this.buffer = '';
    }

    connect() {
        if (this.socket) {
            return;
        }

        this.socket = net.createConnection(this.socketPath, () => {
            console.log('Connected to labwc compositor');
            this.emit('connected');
            
            // Request initial window list
            this.send({ cmd: 'list' });
            
            // Disable labwc's built-in decorations
            this.send({ cmd: 'enable_decorations' });
        });

        this.socket.on('data', (data) => {
            this.handleData(data.toString());
        });

        this.socket.on('error', (err) => {
            console.error('Socket error:', err.message);
            this.emit('error', err);
        });

        this.socket.on('close', () => {
            console.log('Disconnected from labwc compositor');
            this.socket = null;
            this.emit('disconnected');
            
            // Auto-reconnect after 1 second
            if (!this.reconnectTimer) {
                this.reconnectTimer = setTimeout(() => {
                    this.reconnectTimer = null;
                    this.connect();
                }, 1000);
            }
        });
    }

    disconnect() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        
        if (this.socket) {
            this.socket.destroy();
            this.socket = null;
        }
    }

    handleData(data) {
        this.buffer += data;
        
        // Process complete lines (newline-delimited JSON)
        const lines = this.buffer.split('\n');
        this.buffer = lines.pop(); // Keep incomplete line in buffer
        
        for (const line of lines) {
            if (!line.trim()) continue;
            
            try {
                const msg = JSON.parse(line);
                this.handleMessage(msg);
            } catch (err) {
                console.error('Failed to parse message:', line, err);
            }
        }
    }

    handleMessage(msg) {
        switch (msg.event) {
            case 'cursor':
                this.cursorX = msg.x;
                this.cursorY = msg.y;
                this.emit('cursor', { x: msg.x, y: msg.y });
                break;
                
            case 'window_list':
                this.windows.clear();
                for (const win of msg.windows) {
                    this.windows.set(win.id, win);
                }
                this.emit('window_list', Array.from(this.windows.values()));
                break;
                
            case 'mapped':
                this.windows.set(msg.id, msg);
                this.emit('window_created', msg);
                break;
                
            case 'unmapped':
            case 'closed':
                this.windows.delete(msg.id);
                this.emit('window_closed', msg);
                break;
                
            case 'moved':
                if (this.windows.has(msg.id)) {
                    Object.assign(this.windows.get(msg.id), msg);
                    this.emit('window_moved', msg);
                }
                break;
                
            case 'focused':
                this.emit('window_focused', msg);
                break;
                
            case 'title_changed':
                if (this.windows.has(msg.id)) {
                    this.windows.get(msg.id).title = msg.title;
                    this.emit('window_title_changed', msg);
                }
                break;
                
            case 'minimized':
            case 'maximized':
            case 'fullscreen':
                if (this.windows.has(msg.id)) {
                    Object.assign(this.windows.get(msg.id), msg);
                    this.emit('window_state_changed', msg);
                }
                break;
                
            case 'decorations_disabled':
                this.emit('decorations_disabled');
                break;
                
            default:
                console.warn('Unknown event:', msg.event);
        }
    }

    send(command) {
        if (!this.socket) {
            console.warn('Not connected to compositor');
            return false;
        }
        
        const msg = JSON.stringify(command) + '\n';
        this.socket.write(msg);
        return true;
    }

    // Window control methods
    closeWindow(windowId) {
        return this.send({ cmd: 'close', id: windowId });
    }

    minimizeWindow(windowId) {
        return this.send({ cmd: 'minimize', id: windowId });
    }

    maximizeWindow(windowId) {
        return this.send({ cmd: 'maximize', id: windowId });
    }

    moveWindow(windowId, x, y, width, height) {
        return this.send({
            cmd: 'move',
            id: windowId,
            x: Math.round(x),
            y: Math.round(y),
            width: Math.round(width),
            height: Math.round(height)
        });
    }

    focusWindow(windowId) {
        return this.send({ cmd: 'focus', id: windowId });
    }

    setAlwaysOnTop(windowId) {
        return this.send({ cmd: 'always_on_top', id: windowId });
    }

    setAlwaysOnBottom(windowId) {
        return this.send({ cmd: 'always_on_bottom', id: windowId });
    }

    getWindows() {
        return Array.from(this.windows.values());
    }

    getCursorPosition() {
        return { x: this.cursorX, y: this.cursorY };
    }
}

module.exports = LabwcClient;