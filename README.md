# Multi-Cell LTE/5G Simulation (NS-3)

This repository contains an NS-3 simulation script (`cellular_city_multicell_sim.cc`) that models a multi-cell 4G/5G network with configurable:

- Number of users (UEs)
- Number of cells (eNodeBs)
- City area (in meters)
- Simulation time
- Technology profile (4G or 5G)

The script measures:

- Delay
- Jitter
- Throughput
- Packet loss
- Total packets sent/received

---

## Requirements

- NS-3.36 or newer (tested on ns-3.46.1)
- CMake build system
- C++17 compatible compiler

---
## How to Build

```bash
cd <path-to-ns3>
mkdir -p build
cd build
cmake ..
cmake --build .
```

## How to Run
```
./ns3.cellular_city_multicell_sim \
  --tech=4g \
  --nUes=200 \
  --nEnbs=7 \
  --areaSize=2000 \
  --simTime=60
```

