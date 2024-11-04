#!/usr/bin/env -Sawk -f
$3 == "trace_child" {
  tracee_pid = $4
}

$2 == tracee_pid &&
$3 == "term_sig" {
  status = "signaled"
  exit
}
$2 == tracee_pid &&
$3 == "exit_status" {
  status = "exited"
  exit
}
$2 == tracee_pid &&
$3 == "killed" {
  status = "killed"
  exit
}

END {
  if (!status) status = "running"
  print status 
}
