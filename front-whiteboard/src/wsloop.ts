const retryPattern = [0.25, 0.5, 1, 1, 2, 3, 5, 7];

export default class WebSocketLoop {
  url: string;
  ws?: WebSocket;
  onOpen?: () => void;
  onClose?: () => void;
  onMessage?: (data: any) => void;
  reconnectTimer?: ReturnType<typeof setTimeout>;
  active: boolean = false;
  retryIter: number = 0;

  constructor(url: string) {
    this.url = url;
  }

  connect(): void {
    if (this.ws) {
      return;
    }

    delete this.reconnectTimer;
    this.active = true;
    this.ws = new WebSocket(this.url);
    
    this.ws.addEventListener("open", () => {
      this.retryIter = 0;
      if (this.onOpen) {
        this.onOpen();
      }
    });

    this.ws.addEventListener("message", (msg) => {
      if (this.onMessage) {
        this.onMessage(JSON.parse(msg.data));
      }
    });
    
    this.ws.addEventListener("close", () => {
      delete this.ws;
      
      if (!this.reconnectTimer && this.active) {
        const delay = retryPattern[Math.min(this.retryIter++, retryPattern.length - 1)];
        this.reconnectTimer = setTimeout(() => this.connect(), delay * 1000);
      }

      if (this.onClose) {
        this.onClose();
      }
    });
  }

  disconnect(): void {
    this.active = false;

    clearTimeout(this.reconnectTimer!);
    delete this.reconnectTimer;

    this.ws?.close();
    delete this.ws;
  }

  send(data: any): boolean {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(data));
      return true;
    }
    return false;
  }
}