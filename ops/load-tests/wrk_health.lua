local path = os.getenv("WRK_PATH") or "/health"

request = function()
  local headers = {
    ["X-Request-Id"] = "wrk-health"
  }
  for key, value in pairs(wrk.headers) do
    headers[key] = value
  end
  return wrk.format("GET", path, headers)
end
