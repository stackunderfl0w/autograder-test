#!/usr/bin/env -Sawk -f
$3 == "trace_child" {
  tracee_pid = $4
}

$2 == tracee_pid &&
$3 == "exit_status" {
  exitstatus = $4
  exit
}

END {
  if (exitstatus != "") {
    print exitstatus
  } else {
    exit 1
  }
}
