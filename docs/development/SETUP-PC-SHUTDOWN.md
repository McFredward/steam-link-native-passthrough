# Installer0.3.21: process queries during PC shutdown

The0.3.20 failure reported on November8 happened at `runtime_stop`, before the
Frame files were prepared. The requested versions were PC0.3.13 and Frame0.3.7.
Setup then rolled back and restored Frame0.3.5. Read-only inspection confirmed
that its previous library was still installed and mapped. That run did not test
the new Frame shader path.

`check_bb3c91bd1a919545` identifies the authored check
`PC runtime executable path could not be checked.` The previous implementation
handled a process that had already exited. It could still fail when the image
query became unavailable before the same process handle signaled termination.
The supplied log does not include the original Win32 error, so this race is a
reproduced failure mechanism rather than a claim about that missing error code.

Installer0.3.21 waits at most500ms on that same handle after an unsuccessful
image query. If the handle signals exit, there is no remaining process to stop.
Otherwise it retries the image query once and still requires the canonical path,
owner and creation time before returning any process authority. A live,
unreadable process remains a failure; its Win32 code is now retained in the
structured log. No unverified process is terminated. See Microsoft's
[process-handle wait contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject).

The focused control executes the released0.3.20 inspection method with an injected
delayed-exit Win32 port and reproduces the exact reported check. The updated
method accepts only the same handle's confirmed exit. Tests also cover successful
re-query with fresh path/creation verification, persistent live access denial,
failed waits, single handle closure and structured Win32 error reporting.

Only the installer version advances to0.3.21. The PC0.3.13 and Frame0.3.7 native
binaries, launcher and decoder libraries remain byte-identical to candidate0.3.20.
Its Frame shader/GPU evidence is inherited by exact artifact and source identity.
The actual Frame-beta headset test remains outstanding until installation of
Frame0.3.7 is confirmed.
