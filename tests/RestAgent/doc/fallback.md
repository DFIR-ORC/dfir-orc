# Fallback

`<upload>` is a list: configure several and they are tried **in order** until one succeeds. If an entry fails —
its agent can't initialise, or its requests fail (e.g. the server is unreachable) — DFIR-ORC logs the error and
advances to the next entry. The file is uploaded once, by the first entry that works. (One caveat: an
*asynchronous* entry hands off to BITS and its outcome cannot be confirmed in-process, so any fallbacks after it
are still attempted but their value is unpredictable — keep async entries last.)

`rest-fallback.xml` exercises this with two entries: the first points at a dead port (`127.0.0.1:9`, no listener)
and the second is the live sink. Run it against the normal sink
(`python rest_test_server.py --port 8089 --out .\received`) and expect:

- Each file's request to the primary fails with a WinHTTP connection error (`0x80072efd`), so attempt #1 ends
  with `Failed to upload some file(s)` / `Upload #1 failed after 1 attempt(s), trying next upload...`.
- DFIR-ORC then retries the whole set against the fallback: the sink logs one `PUT` per file returning 200, and
  `Upload completed successfully (upload #2, attempt 1/1)` — the reassembled files appear under `received\up\`.
- The body lands **only** via the fallback; the dead-port primary never reaches a listener. The run still
  reports the primary's per-file errors (non-zero error count) even though the fallback rescued the data.

## Retrying one entry before falling back to the next

By default an `<upload>` entry gets exactly one attempt. Two optional attributes buy it more, which is
what a destination that is briefly unreachable (a flapping link, a proxy restart, a server under load
during a mass collection) needs: falling straight through to a different destination, or to none at
all, throws away a transfer that would have worked seconds later.

| Attribute | Required | Meaning |
|-----------|----------|---------|
| `retry` | no (default `0`) | **additional** attempts at this entry after the first, so `retry="2"` means three attempts in all and `retry="0"` is the historical behaviour |
| `retry_delay` | no (default `30`) | seconds to wait between two attempts at the same entry |

```xml
<upload method="REST" server="https://sink.example:8443" retry="2" retry_delay="30">
    <rest http_method="PUT" path="/up/{upload.filename}" body="{upload.local_path}"/>
</upload>
```

Retries are **exhausted before the next entry is tried**, never round-robin: entry #1 gets its
`retry + 1` attempts, then entry #2 starts with its own `retry`/`retry_delay`. Both attributes are
per-entry, and they apply to every upload method, not only REST.

Each attempt runs a **fresh agent**. That is not tidiness: `RESTAgent` latches a rejected
authentication (to avoid locking the account out) and a failed session, so it deliberately refuses to
try either again, and retrying inside the same agent would only spin on the same error.

A retry does not re-send what that entry already took. Per-file successes are read back from the agent
after `agent::wait`, and a file already delivered to this entry is skipped on the next attempt
(`already succeeded at this entry; not replayed`, at debug level). Two deliberate limits:

- The skip set is **per entry**. Moving to a fallback replays everything, because the new destination
  has received none of it.
- It is not used for BITS, which answers the upload request as soon as the file joins the job and only
  reports the real outcome later. A file BITS called added may never have left the host, so skipping it
  on the retry meant to deliver it would lose it; BITS entries replay the whole set instead.

A request whose local file is gone is dropped too (`the local file is gone`). With `operation="move"`
the first attempt deletes what it uploaded, so without this a partial success would condemn every
later attempt, and every fallback, to fail on files that no longer exist.

### The bounds, and why they are there

`CompleteUpload` runs on the main thread once collection is over, after the log file is closed, and
both the wait and the `agent::wait` around it are uninterruptible. `retry x retry_delay` is therefore
quite literally how long the process refuses to exit at the end of a run, so it is capped rather than
left to a typo:

| Rule | Value |
|------|-------|
| `retry` | at most 10 |
| `retry_delay` | at most 1800 seconds |
| `retry x retry_delay` | at most 1800 seconds per entry |

The budget is the rule that matters; the two per-attribute ceilings only let a refusal name the
obviously wrong attribute (a `retry_delay` written in milliseconds, say). Worst case is therefore
30 minutes of waiting per configured entry. Keep it well under that if the collection runs under an
orchestrator or EDR timeout: a process killed mid-wait never reaches its later fallback entries, so a
long retry budget trades fallback coverage for retry coverage.

Anything unreadable is **refused**, not quietly defaulted: `retry="two"`, `retry="-1"`, a delay past
the cap, or a budget over it all fail `Validate()`, which `ToolEmbed` runs when the configuration is
embedded. A mistyped retry policy is reported at embed time instead of vanishing into a default the
operator never wrote.

`retry` with `mode="async"` is **warned about, not refused**. ToolEmbed prints

```
ToolEmbed: '<config>': 'retry' has no effect with mode="async": an asynchronous upload finishes
after DFIR-ORC exits, so its outcome is never observed and no attempt can be retried
```

and embeds the configuration anyway. The attribute is simply ignored on that entry: an async entry
hands off to BITS, and `BITSAgent::IsComplete` answers "done, no failure" as soon as the agent is
ready to exit, without looking at the jobs at all, so no attempt is ever seen to fail. The launcher
forces such an entry to a single attempt to match, rather than re-queueing a transfer that may still
be running. It is a warning rather than a refusal because the setting is harmless and an operator
writing one `retry` across a list may well have meant it for a synchronous fallback further down.

Values are read in base 10, so `retry_delay="010"` is ten seconds and not eight.

### Which files are retried

All of them, including the run's own artifacts. Every file the run produces is recorded as it is
produced (the archives, and the outline, outcome, console tee and log that go through
`UploadSingleFile`), and every one of those calls happens before `CompleteUpload` runs. A retry
replays the whole recorded set, minus what this entry already took and minus anything no longer on
disk, so a log file whose live upload failed is attempted again on every later attempt exactly like an
archive.

What retry does **not** do is re-attempt a file at the moment it fails. The live send is asynchronous
(`Concurrency::send` into the agent's queue), so nothing observes that failure while the collection is
still running: every retry happens at the end of the run, in `CompleteUpload`. For a destination that
is down that is the better order anyway, since the delay is spent collecting rather than waiting.

### What the tags prove

| Tag | Fixture | Verdict |
|-----|---------|---------|
| `retry-exhausted` | `vm-retry-exhausted.xml` | one entry at a dead port, `retry="2" retry_delay="1"`: nothing reaches the sink, and `attempt 2/3 failed; retrying in` proves all three attempts really happened |
| `retry-then-fallback` | `vm-retry-then-fallback.xml` | dead entry with `retry="1"`, live sink second: byte-exact at the sink, and `Upload #1 failed after 2 attempt(s)` proves entry #1 was exhausted first |

`retry-exhausted` is scored on the log, not on the empty sink: a build with no retry at all also
leaves the sink empty, so the empty sink alone would pass a broken implementation.

Neither tag proves a retry can **succeed**, because the bench has no endpoint that fails once and then
serves. That would need a `--fail-once` mode in `rest_test_server.py` (answer the first request per
`(method, path)` with a 503, serve it on the next). The per-entry skip set is likewise uncovered: it
needs a *partial* first attempt, so a first attempt that fails on one file only. Both are gaps worth
knowing about.

`MakeReplayRequest` and `CompleteUpload` live in `OrcCommand`, which has no unit-test project, so they
are covered by these tags alone. The parsing and the `Validate()` rules are unit-tested in
`tests/OrcLibTest/upload_validate_test.cpp` (`UploadRetryPolicyTest`).
