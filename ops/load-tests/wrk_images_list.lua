local path = os.getenv("WRK_PATH") or "/api/images/my-list?page=0&size=10"

request = function()
  local headers = {
    ["X-Request-Id"] = "wrk-images-list"
  }
  for key, value in pairs(wrk.headers) do
    headers[key] = value
  end
  local token = os.getenv("AUTH_TOKEN")
  if token and token ~= "" then
    headers["Authorization"] = "Bearer " .. token
  end
  return wrk.format("GET", path, headers)
end
