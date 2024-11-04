#!/usr/bin/env -Sawk -f
$3 == "trace_end" { 
  endtime= $1
  exit 0
}
END {
  if (endtime != "") {
    print endtime
  } else {
    exit 1
  }
}
