---
name: "Bad Write"
description:
  Stability test for sys_write.
tags: [badcall,stability]
depends: [shell]
sys161:
  ram: 2M
---
khu
$ /testbin/badcall e
khu
