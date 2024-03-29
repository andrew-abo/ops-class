---
name: "Parallel VM 32 Cores (Swap)"
description: >
  Stress tests your VM by performing various matrix computations in
  multiple concurrent processes.
tags: [swap]
depends: [swap-basic, /vm/parallelvm.t, shell]
sys161:
  cpus: 32
  ram: 1M
  disk1:
    enabled: true
monitor:
  progresstimeout: 30.0
  commandtimeout: 3000.0
  window: 40
misc:
  prompttimeout: 3600.0
stat:
  resolution: 0.1
---
$ /testbin/parallelvm -w
