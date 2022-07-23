---
name: "Bad Seek"
description:
  Stability test for sys_lseek.
tags: [badcall,stability]
depends: [shell]
sys161:
  ram: 2M
---
khu
$ /testbin/badcall j
khu
