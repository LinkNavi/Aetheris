set pagination off
set logging file /tmp/crash.txt
set logging overwrite on
set logging redirect on
set logging enabled on

handle SIGBUS stop print
handle SIGSEGV stop print

set print thread-events off

run

bt full
info registers
x/20i $pc-40

set logging enabled off
quit
