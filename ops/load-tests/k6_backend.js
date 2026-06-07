import http from 'k6/http';
import { check, group, sleep } from 'k6';

const baseUrl = __ENV.BASE_URL || 'http://127.0.0.1:8080';
const vus = Number(__ENV.VUS || '10');
const duration = __ENV.DURATION || '1m';
const enableCreate = (__ENV.ENABLE_CREATE || '').toLowerCase() === 'true';
const createSleepMs = Number(__ENV.CREATE_RATE_SLEEP_MS || '1000');

export const options = {
  vus,
  duration,
  thresholds: {
    http_req_failed: ['rate<0.05'],
    http_req_duration: ['p(95)<1000'],
  },
};

function authHeaders(token) {
  const headers = {
    'Content-Type': 'application/json',
    'X-Request-Id': `k6-${__VU}-${__ITER}`,
  };
  if (token) {
    headers.Authorization = `Bearer ${token}`;
  }
  return { headers };
}

export function setup() {
  if (__ENV.AUTH_TOKEN) {
    return { token: __ENV.AUTH_TOKEN };
  }
  if (!__ENV.LOGIN_USERNAME || !__ENV.LOGIN_PASSWORD) {
    return { token: '' };
  }

  const response = http.post(
    `${baseUrl}/api/auth/login`,
    JSON.stringify({
      username: __ENV.LOGIN_USERNAME,
      password: __ENV.LOGIN_PASSWORD,
    }),
    authHeaders('')
  );

  check(response, {
    'login returned 200': (r) => r.status === 200,
    'login returned access token': (r) => Boolean(r.json('access_token')),
  });

  return { token: response.json('access_token') || '' };
}

export default function (data) {
  group('health', () => {
    const response = http.get(`${baseUrl}/health`, authHeaders(data.token));
    check(response, {
      'health is 200 or 503': (r) => r.status === 200 || r.status === 503,
      'health has request id': (r) => Boolean(r.headers['X-Request-Id']),
    });
  });

  group('image health', () => {
    const response = http.get(`${baseUrl}/api/images/health`, authHeaders(data.token));
    check(response, {
      'image health is 200': (r) => r.status === 200,
      'image health has request id': (r) => Boolean(r.headers['X-Request-Id']),
    });
  });

  if (data.token) {
    group('my image list', () => {
      const response = http.get(`${baseUrl}/api/images/my-list?page=0&size=10`, authHeaders(data.token));
      check(response, {
        'list is 200': (r) => r.status === 200,
      });
    });
  }

  if (enableCreate && data.token) {
    group('create image task', () => {
      const requestId = `k6-${Date.now()}-${__VU}-${__ITER}`;
      const response = http.post(
        `${baseUrl}/api/images`,
        JSON.stringify({
          prompt: __ENV.PROMPT || 'load test prompt',
          request_id: requestId,
          num_steps: Number(__ENV.NUM_STEPS || '4'),
          height: Number(__ENV.HEIGHT || '512'),
          width: Number(__ENV.WIDTH || '512'),
        }),
        authHeaders(data.token)
      );
      check(response, {
        'create is accepted or rate limited': (r) => r.status === 202 || r.status === 429,
      });
      sleep(createSleepMs / 1000);
    });
  }

  sleep(Number(__ENV.SLEEP_SECONDS || '1'));
}
