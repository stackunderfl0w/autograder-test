#!/usr/bin/env -Sawk -f
$3 == "fork_child" {
  print $4
}
