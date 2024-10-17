#!/usr/bin/env -Sawk -f
$3 == "trace_child" { 
  starttime= $1
  exit
}
END {
  if (starttime) {
    print starttime
  } else {
    exit 1
  }
}
