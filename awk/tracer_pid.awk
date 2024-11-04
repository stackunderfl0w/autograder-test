#!/usr/bin/env -Sawk -f
$3 == "trace_child" {
  print $2
  exit 0
}
END {
  exit 1
}
