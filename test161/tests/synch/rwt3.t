---
name: "RW Lock Test 3"
description:
  Tests reader-writer releasing unheld write lock panics correctly.
  Should panic.
tags: [synch, rwlocks]
depends: [boot]
sys161:
  cpus: 1
---
rwt3
