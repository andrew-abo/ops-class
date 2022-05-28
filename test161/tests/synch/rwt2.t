---
name: "RW Lock Test 2"
description:
  Tests that releasing reader-writer unheld read lock panics correctly.
  Should panic.
tags: [synch, rwlocks]
depends: [boot, semaphores, cvs]
sys161:
  cpus: 1
---
rwt2

