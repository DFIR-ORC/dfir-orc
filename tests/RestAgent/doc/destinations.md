# Several destinations

A list of `<upload>` entries is one destination with spares (see [fallback.md](fallback.md)). The
`<uploads>` block is the other axis: several **destinations**, each one a fallback chain of its own,
each receiving its own copy of the collection.

```xml
<uploads>
    <destination name="hq">
        <upload method="REST" server="https://hq1.example" operation="copy">
            <rest http_method="PUT" path="/up/{upload.filename}" body="{upload.local_path}"/>
        </upload>
        <upload method="REST" server="https://hq2.example" operation="copy" retry="2">
            <rest http_method="PUT" path="/up/{upload.filename}" body="{upload.local_path}"/>
        </upload>
    </destination>
    <destination name="nas" required="no" include="GetThis*">
        <upload method="FILECOPY" path="\\nas\drop" operation="copy"/>
    </destination>
</uploads>
```

`hq` gets everything, falling back to `hq2` if `hq1` fails after three attempts. `nas` gets the
GetThis archives only, and its failure does not fail the run.

A top-level `<upload>` list keeps meaning exactly what it has always meant: one destination, its
siblings are its fallback chain. Nothing about an existing configuration changes. Using **both** forms
in one configuration is refused rather than merged, because guessing which the author meant is how a
collection gets lost.

## Destinations run one after another

They are **sequential, never parallel**, to protect the uplink of the collected machine: a host being
collected is usually a host someone still has to work on, and two or three simultaneous uploads of a
multi-gigabyte archive is not a reasonable thing to do to it.

Three consequences worth planning around:

- **Order matters.** The first destination's first entry is the only one that overlaps with the
  collection: archives stream to it as each command finishes. Every later destination is served after
  the collection ends, by replaying the same recorded files. Put the destination that matters most, or
  the one on the fattest link, first.
- **Wall-clock is a sum.** Two destinations for a 10 GB collection is roughly twice the
  post-collection upload time.
- **A slow destination gates the ones behind it.** An early destination burning `retry="3"
  retry_delay="60"` holds the next one for three minutes before it even starts. Keep retry budgets
  modest on anything that is not the last destination.

Parallel destinations are deliberately not offered. The `<uploads>` shape can carry them later without
a configuration change, but it would mean several live upload agents at once, which is the cost this
design exists to avoid.

## Attributes

### On `<destination>`

| Attribute | Required | Meaning |
|-----------|----------|---------|
| `name` | **yes** | names the destination in every log line and every refusal. Two destinations differing only in case (`hq` and `HQ`) are refused: they would be indistinguishable in the log |
| `required` | no (default `yes`) | a required destination receiving nothing ends the run with `Critical`; an optional one with `Error` |
| `artifacts` | no (default `yes`) | whether the four files the collection writes about itself (outline, outcome, log, console tee) reach this destination |
| `include` / `exclude` | no | the filters for this destination's chain, inherited by its `<upload>` entries |

### On `<uploads>`

| Attribute | Required | Meaning |
|-----------|----------|---------|
| `include` / `exclude` | no | the filters every destination inherits unless it writes its own |
| `delete_when_done` | no (default `no`) | delete each local file once every required destination has confirmed it |

## What the run reports

The verdict is decided **once**, after every destination has run. A required destination failing does
not stop, skip or shorten any other one: it never costs a peer its copy.

| Outcome | Level |
|---------|-------|
| every destination received the collection | `Info` |
| only `required="no"` destinations failed | `Error`, naming them |
| any `required="yes"` destination received nothing | `Critical`, naming them |

A destination whose filters matched nothing is **not** a failure. It logs a warning
(`no file matched its filters, nothing sent`), because a filter typo that matches nothing should be
visible without condemning a collection that otherwise went fine.

A legacy top-level `<upload>` list reports exactly as it always has, down to the wording of its
`Critical` and its `Upload #2` labels.

## Filters: three levels, nearest wins

`include` and `exclude` can be written on `<uploads>`, on `<destination>`, and on `<upload>`. Each
attribute is resolved independently, and the nearest level that writes it wins:

```xml
<uploads exclude="*.tmp">
    <destination name="hq" include="*.7z">
        <!-- include="*.7z" from the destination, exclude="*.tmp" inherited -->
        <upload method="REST" server="https://hq.example"/>
    </destination>
    <destination name="nas" include="*.7z" exclude="*scratch*">
        <!-- its own exclude REPLACES "*.tmp" -->
        <upload method="FILECOPY" path="\\nas\drop"/>
    </destination>
</uploads>
```

Two details that are easy to get wrong:

- **`exclude` overrides, it does not accumulate.** A destination that writes its own `exclude`
  silently re-enables whatever an outer level was blocking. If an exclusion must hold everywhere,
  write it on every level that overrides it.
- **An empty attribute is a decision, not an absence.** `include=""` means "deliberately no include
  filter here, do not inherit one". Resolution keys on whether the attribute was written at all, not
  on whether its value is empty. A stray separator is dropped rather than becoming a pattern:
  `include="*.7z,"` is one pattern, not one pattern and an empty one that would match nothing useful.

Run artifacts ignore all of this. They are the collection's own record rather than collected data, so
they reach every destination whatever its filters say; `artifacts="no"` is the only thing that keeps
them away from one.

## delete_when_done

With more than one destination, `operation="move"` is **refused at configuration time**, and again at
embed time by ToolEmbed. Move deletes the local file as soon as one destination has taken it, which
leaves every other destination nothing to send.

`<uploads delete_when_done="yes">` is the replacement, and it is strictly safer: it runs once, after
every destination has finished, and deletes a local file only when **every required destination that
wanted it** confirmed receipt. Anything a required destination missed stays on the host.

It fails closed in every ambiguous case, because it removes evidence:

- no `required="yes"` destination in the configuration at all: nothing is deleted
- a file no required destination wanted: kept, because nothing ever uploaded it
- a destination served by **BITS**: kept. BITS answers the upload request as soon as the file joins
  the job and reports the real outcome only later, so it cannot confirm a transfer per file. The run
  says so once rather than leaving the files unexplained.

`operation="move"` keeps working unchanged in a single-destination configuration, including a move
inside a fallback chain.

## What is refused

All of these are decided when the configuration is read, and again at embed time, so a bad
configuration fails on the operator's workstation instead of on a collected host:

| Condition | Outcome |
|-----------|---------|
| `operation="move"` with more than one destination | refused |
| two destinations with the same `name` (case-insensitively) | refused |
| both a top-level `<upload>` list and an `<uploads>` block | refused |
| `<uploads>` with no `<destination>` | refused |
| a `<destination>` with no usable `<upload>`, when it is `required` | refused |
| a `<destination>` with no usable `<upload>`, when it is optional | dropped, with an error logged |
| no destination left after the drops | refused |

A required destination that cannot be configured is refused rather than dropped on purpose: dropping
it would quietly reduce the redundancy the operator asked for, and the run would then end `Info` with
one copy where two were configured.

## Trying it locally

`rest-destinations.xml` runs two destinations against two local sinks, each writing to its own
directory. Two destinations writing to one directory cannot be told apart, and "both received it" is
the whole assertion:

```
python rest_test_server.py --port 8089 --out .\received
python rest_test_server.py --port 8090 --out .\received2
```

`hq` is required and falls back from a dead port to the sink on 8089. `nas` is optional, takes no run
artifacts, and lands on 8090. Expect `received\up\` to hold the archive plus the run's own files, and
`received2\up\` to hold the archive alone.
