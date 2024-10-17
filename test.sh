#!/usr/bin/env bash
trap '' SIGINT

(
trap - SIGINT
bin/check_signal_disposition SIGINT SIGTSTP SIGTTOU
)
