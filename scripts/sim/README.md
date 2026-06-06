# SOLAR ESP32 API Simulator

This simulator mirrors the ESP32 HTTP API closely enough to test the gateway,
mobile/control-panel UI calls, and host RL runner without the board connected.
It does not simulate servo physics or camera optics.

Run from the repo root:

```powershell
python .\scripts\sim\esp32_api_sim.py --host 127.0.0.1 --port 8081
```

Then point local tools at `http://127.0.0.1:8081`.

Benchmark the hot API paths:

```powershell
python .\scripts\sim\benchmark_api.py --robot-url http://127.0.0.1:8081 --loops 200
```

For the host policy runner:

```powershell
$env:PYTHONPATH = "$PWD\scripts\rl"
python -m solar_rl.runner --robot-url http://127.0.0.1:8081 --duration 10 --policy solar-seek
```

For the remote gateway with separate gateway and robot tokens:

```powershell
$env:PORT = "8787"
$env:ROBOT_BASE_URL = "http://127.0.0.1:8081"
$env:GATEWAY_TOKEN = "gateway-token"
$env:ROBOT_API_TOKEN = "robot-token"
node .\remote-gateway\server.js
```

Start the simulator with the matching robot token:

```powershell
python .\scripts\sim\esp32_api_sim.py --host 127.0.0.1 --port 8081 --api-token robot-token
```
