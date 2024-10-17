#!/usr/bin/env -Sawk -f
$3 == "trace_child" {
  tracee_pid = $4
}

$2 == tracee_pid &&
$3 == "term_sig" {
  termsig = $5
  exit
}

END {
  if (termsig != "") {
    print termsig
  } else {
    exit 1
  }
}
