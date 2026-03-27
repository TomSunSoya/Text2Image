const listeners = new Set();

let socket = null;
let reconnectTimer = 0;
let reconnectDelayMs = 1000;
let manualClose = false;

const dispatch = (event) => {
  listeners.forEach((listener) => {
    try {
      listener(event);
    } catch (error) {
      console.error('Task socket listener failed:', error);
    }
  });
};

const clearReconnectTimer = () => {
  if (reconnectTimer) {
    window.clearTimeout(reconnectTimer);
    reconnectTimer = 0;
  }
};

const buildSocketUrl = () => {
  const token = localStorage.getItem('token');
  if (!token) {
    return '';
  }

  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${protocol}//${window.location.host}/api/ws/images?token=${encodeURIComponent(token)}`;
};

const scheduleReconnect = () => {
  if (manualClose || reconnectTimer || listeners.size === 0) {
    return;
  }

  reconnectTimer = window.setTimeout(() => {
    reconnectTimer = 0;
    ensureTaskSocketConnected();
  }, reconnectDelayMs);
  reconnectDelayMs = Math.min(reconnectDelayMs * 2, 5000);
};

export const ensureTaskSocketConnected = () => {
  if (
    socket &&
    (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)
  ) {
    return;
  }

  const url = buildSocketUrl();
  if (!url) {
    return;
  }

  manualClose = false;
  socket = new WebSocket(url);

  socket.onopen = () => {
    reconnectDelayMs = 1000;
    dispatch({ type: 'image.task.socket.open' });
  };

  socket.onmessage = (event) => {
    try {
      const payload = JSON.parse(event.data);
      dispatch(payload);
    } catch (error) {
      console.error('Failed to parse task socket payload:', error);
    }
  };

  socket.onerror = (error) => {
    console.error('Task socket error:', error);
  };

  socket.onclose = (event) => {
    socket = null;
    dispatch({ type: 'image.task.socket.close', code: event.code, reason: event.reason });
    if (event.code === 1008) {
      return;
    }
    scheduleReconnect();
  };
};

export const subscribeTaskEvents = (listener) => {
  listeners.add(listener);
  ensureTaskSocketConnected();

  return () => {
    listeners.delete(listener);
    if (listeners.size === 0) {
      closeTaskSocket();
    }
  };
};

export const closeTaskSocket = () => {
  manualClose = true;
  clearReconnectTimer();

  if (!socket) {
    return;
  }

  const activeSocket = socket;
  socket = null;
  if (
    activeSocket.readyState === WebSocket.OPEN ||
    activeSocket.readyState === WebSocket.CONNECTING
  ) {
    activeSocket.close();
  }
};
