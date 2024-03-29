---
name: "Big Fork"
description: >
  Stress tests your VM system by performing various matrix computations in
  multiple concurrent processes. This test is a cross between parallelvm
  and forktest.
tags: [vm]
depends: [not-dumbvm-vm, /syscalls/forktest.t]
sys161:
  cpus: 4
  ram: 8M
monitor:
  progresstimeout: 40.0
  commandtimeout: 3000.0
  window: 40  
---
khu
$ /testbin/bigfork
khu
