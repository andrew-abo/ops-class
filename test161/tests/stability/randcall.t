---
name: "Randcall Test"
description: >
  Invokes system calls with random arguments.
tags: [stability]
depends: [shell]
sys161:
  ram: 16M
---
khu
$ /testbin/randcall -f -c 100 -r 421 2
khu
