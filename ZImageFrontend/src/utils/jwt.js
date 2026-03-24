export function decodeJwtPayload(token) {
  if (!token || typeof token !== 'string') {
    return null;
  }

  const parts = token.split('.');
  if (parts.length < 2) {
    return null;
  }

  try {
    if (typeof globalThis.atob !== 'function') {
      return null;
    }

    const normalized = parts[1]
      .replace(/-/g, '+')
      .replace(/_/g, '/')
      .padEnd(Math.ceil(parts[1].length / 4) * 4, '=');

    return JSON.parse(globalThis.atob(normalized));
  } catch {
    return null;
  }
}

export function isTokenExpired(token) {
  const payload = decodeJwtPayload(token);
  if (!payload || typeof payload.exp !== 'number') {
    return false;
  }

  return payload.exp * 1000 <= Date.now();
}

export function clearStoredAuth() {
  localStorage.removeItem('token');
  localStorage.removeItem('userInfo');
}
