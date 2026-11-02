# Python policy for bpm

Python files are ignored by default. New production tooling belongs in Buoy or BuoyScript.

The following exact files remain temporary production compatibility exceptions:

| File | Required capability |
| --- | --- |
| `src/registry/build.py` | Only current builder for the optional Linux reference registry server and its native storage bridge. |
| `tools/build_bpm.py` | Unported macOS, Windows and cross-target BPM bootstrap, including platform runtime deployment; Linux uses build_bpm.bui. |
| `tools/build_bpm_host.py` | Native bridge compilation and packaging dependency of the unported macOS/Windows BPM bootstrap. |

An exception may be removed after its native replacement passes the corresponding build or runtime checks.
Review additions file by file; directory-wide and wildcard Python exceptions are not allowed.
