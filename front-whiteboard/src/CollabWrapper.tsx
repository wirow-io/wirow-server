import React, { useCallback, useEffect, useState } from 'react';
import Excalidraw from '@excalidraw/excalidraw';
import Collab from './collab';
import './CollabWrapper.css';
import { RouteComponentProps, withRouter } from 'react-router';
import { joinPath } from './utils';
import { ExcalidrawAPIRefValue } from '@excalidraw/excalidraw/types/types';
import { acquireKVDB } from './db';

function CollabWrapper(props: RouteComponentProps) {
  const pathname = props.location.pathname, roomId = props.location.hash.slice(1);

  const [username, setUsername] = useState<string | null>();
  const [wsUrl, setWsUrl] = useState<string>();
  const [ready, setReady] = useState(false);
  const [readonly, setReadonly] = useState(false);
  const [title, setTitle] = useState<string>();
  const [collab, setCollab] = useState<Collab>();
  const [excalidraw, setExcalidraw] = useState<ExcalidrawAPIRefValue>();

  const onPointerUpdate = useCallback((pointer) => collab && collab.onPointerUpdate(pointer), [collab]);

  useEffect(() => {
    document.title = title ? `${title} (whiteboard)` : `Wirow Whiteboard`;
  }, [title]);

  useEffect(() => {
    if (collab) {
      collab.onReady = (val: boolean) => setReady(val);
      collab.onReadonly = (val: boolean) => setReadonly(val);
      collab.onTitle = (val: string) => setTitle(val);
      setReady(collab.isReady);
      setReadonly(collab.isReadonly);
      setTitle(undefined);

      collab.connect();
      return collab.disconnect.bind(collab);
    } else {
      setReady(false);
      setTitle(undefined);
    }
  }, [collab]);

  useEffect(() => {
    if (collab && excalidraw?.ready) {
      collab.excalidraw = excalidraw;
    }
  }, [collab, excalidraw, excalidraw?.ready]);

  useEffect(() => {
    if (wsUrl !== undefined) {
      setCollab(new Collab(wsUrl));
    } else {
      setCollab(undefined);
    }
  }, [wsUrl]);

  useEffect(() => {
    const proto = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    const base = joinPath([proto, window.location.host, pathname, 'ws', roomId]);
    if (username === undefined) {
      setWsUrl(undefined);
    } else if (username === null) {
      setWsUrl(base);
    } else {
      setWsUrl(`${base}?username=${username}`);
    }
  }, [pathname, roomId, username]);

  useEffect(() => {
    acquireKVDB('greenrooms').then(db => {
      return db.get('nickname');
    }).then(name => {
      typeof(name) === 'string' ? setUsername(name) : setUsername(null);
    }).catch(err => {
      console.log('Error occurred', err)
      setUsername(null)
    });
  }, []);

  const active = ready && !!collab;

  return <div className="collab_root">
    <Excalidraw
      detectScroll={false}
      handleKeyboardGlobally={true}
      viewModeEnabled={!active || readonly || undefined}
      isCollaborating={active}
      onPointerUpdate={onPointerUpdate}
      ref={ed => setExcalidraw(ed ?? undefined)}
    />
    {!active && <div className="connecting">Connecting...</div>}
  </div>;
}

export default withRouter(CollabWrapper);