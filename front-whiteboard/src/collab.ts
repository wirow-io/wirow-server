import WebSocketLoop from "./wsloop";
import equal from 'fast-deep-equal';
import { objectDiff } from "./utils";
import { ExcalidrawImperativeAPI } from "@excalidraw/excalidraw/types/types";
import { ExcalidrawElement } from "@excalidraw/excalidraw/types/element/types";

// e is elements, c is collaborators, s is state
type RecvData = {
  sync?: {e: {[key: string]: any}[], c: {[id: string]: {[key: string]: any}}},
  e?: {[id: string]: {isDeleted: boolean, [key: string]: any}},
  c?: {[id: string]: {isDeleted: boolean, [key: string]: any}},
  readonly?: boolean,
  title?: string,
}

type SendData = {
  e?: {[id: string]: {isDeleted: boolean, [key: string]: any}},
  s?: any,
}

export default class Collab {
  excalidraw?: ExcalidrawImperativeAPI;
  isReady: boolean = false;
  isReadonly: boolean = true;

  onReady?: (ready: boolean) => void;
  onReadonly?: (ready: boolean) => void;
  onTitle?: (title: string) => void;

  sync: {state: any, elements: Map<string, any>, collaborators: Map<string, any>};
  actual: {state: any};

  wsUrl: string;
  ws?: WebSocketLoop;

  sendInterval: ReturnType<typeof setInterval>;
  pingInterval: ReturnType<typeof setInterval>;

  constructor(wsUrl: string) {
    this.sync = {
      state: {},
      elements: new Map(),
      collaborators: new Map(),
    };

    this.actual = {
      state: {},
    };

    this.wsUrl = wsUrl;

    this.sendInterval = setInterval(() => this.periodicSend(), 100);
    this.pingInterval = setInterval(() => this.ws && this.ws.send({ping: true}), 5e3);
  }

  onMessage(data: RecvData) {
    if (data.readonly !== undefined) {
      if (data.readonly !== this.isReadonly) {
        this.isReadonly = data.readonly;
        this.onReadonly && this.onReadonly(this.isReadonly);
      }
    }

    if (data.title) {
      this.onTitle && this.onTitle(data.title);
    }

    if (data.sync) {
      this.sync.state = {};
      this.sync.elements.clear();
      this.sync.collaborators.clear();
      
      for (let [id, i] of Object.entries(data.sync.e)) {
        i.id = id;
        this.sync.elements.set(id, {...i});
      }

      for (let [id, i] of Object.entries(data.sync.c)) {
        this.sync.collaborators.set(id, {...i});
      }

      if (this.excalidraw) {
        this.excalidraw.updateScene({
          elements: Object.values(data.sync.e) as ExcalidrawElement[],
          collaborators: this.sync.collaborators,
        });
      }

      this.isReady = true;
      this.onReady && this.onReady(true);
    }

    if (data.e) {
      for (const [id, el] of Object.entries(data.e)) {
        if (el.isDeleted) {
          this.sync.elements.delete(id);
        } else {
          const update = Object.assign(this.sync.elements.get(id) || {}, el, {id});
          this.sync.elements.set(id, update);
        }
      }

      const elements = [...this.excalidraw?.getSceneElementsIncludingDeleted() ?? []];
      for (let i = 0; i < elements.length; i++) {
        const id = elements[i].id;
        if (data.e.hasOwnProperty(id)) {
          elements[i] = {...this.sync.elements.get(id)}
          delete data.e[id];
        }
      }
      for (const id of Object.keys(data.e)) {
        elements.push({...this.sync.elements.get(id)});
      }
      
      if (this.excalidraw) {
        this.excalidraw.history.clear();
        this.excalidraw.updateScene({elements});
      }
    }

    if (data.c) {
      for (const [id, user] of Object.entries(data.c)) {
        if (user.isDeleted) {
          this.sync.collaborators.delete(id);
        } else {
          const update = Object.assign(this.sync.collaborators.get(id) || {}, user);
          this.sync.collaborators.set(id, update);
        }
      }

      if (this.excalidraw) {
        this.excalidraw.updateScene({
          collaborators: this.sync.collaborators,
        });
      }
    }
  }

  onPointerUpdate({pointer, button}: {pointer: { x: number; y: number }, button: "down" | "up"}): void {
    this.actual.state.button = button;
    this.actual.state.pointer = pointer;
  }

  periodicSend(): void {
    if (!this.excalidraw) {
      return;
    }

    const data: SendData = {};

    {
      let elements: any = {};
      for (let i of this.excalidraw?.getSceneElementsIncludingDeleted() || []) {
        if (!i.id) {
          continue;
        }

        const last = this.sync.elements.get(i.id);
        if ((!last && !i.isDeleted) || (last && (last.version !== i.version || last.versionNonce !== i.versionNonce))) {
          elements[i.id] = objectDiff(last || {}, i);
        }
      }

      if (!equal(elements, {})) {
        data.e = elements;
      }
    }

    {
      const rawState = this.excalidraw?.getAppState();
      if (rawState) {
        const state = {
          ...this.actual.state,
          pointer: this.actual.state.pointer && {
            ...this.actual.state.pointer,
            x: Math.round(this.actual.state.pointer.x),
            y: Math.round(this.actual.state.pointer.y),
          },
          selectedElementIds: rawState.selectedElementIds
        };

        const diff = objectDiff(this.sync.state, state);
        if (!equal(diff, {})) {
          data.s = diff;
        }
      }
    }
    
    let sended = false;
    if (!equal(data, {}) && this.ws) {
      sended = this.ws.send(data);
    }

    if (sended) {
      if (data.e) {
        for (let [id, el] of Object.entries(data.e)) {
          if (el.isDeleted) {
            this.sync.elements.delete(id);
          } else {
            const update = Object.assign(this.sync.elements.get(id) || {}, el);
            this.sync.elements.set(id, update);
          }
        }
      }

      if (data.s) {
        Object.assign(this.sync.state, data.s);
      }

      this.excalidraw?.updateScene({elements: this.excalidraw?.getSceneElements()});
    }
  }

  connect(): void {
    this.ws = new WebSocketLoop(this.wsUrl);
    this.ws.onClose = () => this.onReady && this.onReady(false);
    this.ws.onMessage = this.onMessage.bind(this)
    this.ws.connect();
  }

  disconnect(): void {
    this.onReady && this.onReady(false);
    this.ws?.disconnect();
    delete this.ws;
  }
}