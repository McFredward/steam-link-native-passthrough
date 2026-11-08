# Replacing an installed branch with the installer package

A newer room-scan installation kept the same native install receipt schema,
but added two owned systemd paths. The stable installer rejected those paths
while reading the Frame inventory (`Unknown Frame component scope preserved.`,
check `5838c6c539fe316d`). Discovery then reported a connection failure even
though the Frame had answered. A separate version gate also hid installation
when the package contained an older patch.

The installer now reads the known optional room scopes, reports the independently
installed PC and Frame patch versions, and offers **Install this package version**.
The existing Apply question confirms the displayed installed and target versions.
A replacement approval binds the exact package fingerprint and both versions;
a changed source version or package invalidates it. Unversioned target packages
do not replace versioned installations. Existing file identity checks still apply.

A package without Room Setup retires only its recorded components, restores their
original predecessors, and preserves saved room data outside the payload tree.
The recorded dashboard service is stopped before replacement; an active manual
camera reader is stopped through its private, PID-checked local protocol. No
installed Python from an unknown package is executed. The full coordinated
Frame restart and startup/picture checks remain in the existing Apply workflow.

Room-only lifecycle recovery records and the existing 192 MiB checkpoint limit
remain readable, including recovery from an interrupted feature-branch update.
Native PC/Frame payloads and their patch versions do not change for this fix.

A remotely returned validation refusal now reaches the failure presentation with
its locally known check message. Both discovery catch layers preserve that
classification instead of converting it to a missing-device result. SSH and
transport failures retain their separate connection diagnostics; arbitrary
remote exception text never enters the public message or session log.

Regression coverage includes the installed roomscan component versions,
confirmed replacement, an unavailable/stale replacement approval, exact file
and receipt restoration, saved rooms, unrelated external paths and services,
private camera-stop IPC, compact localized action labels, and discovery errors.
Software fixtures do not establish successful installation on the user's Frame.
