---
name: "Bad Close"
description: >
  Stability test for sys_close.
tags: [badcall,stability]
depends: [shell]
sys161:
  ram: 2M
---
khu
$ /testbin/badcall f
khu
