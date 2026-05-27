# Jenkins access

Agents benefit from access to Jenkins (<https://jenkins.wirenboard.com>) — it lets
them check whether a pushed commit built/tested cleanly without re-running
everything locally.

The web UI is behind Google SSO, but the API accepts HTTP Basic auth with a
**personal API token**. To grant an agent access:

1. Jenkins → top-right user menu → **Configure** → **API Token** → **Add new
   token**, copy the value.
2. Give the agent your **Jenkins User ID** (email form, e.g.
   `you.copr.name@wirenboard.com` — *not* the short login) plus the token.
3. Optionally install a Jenkins MCP server so the agent calls it natively
   instead of curl.

Base URL pattern for this repo's branch jobs (note the **double URL-encoding**
of the slash in the branch name — `/` → `%252F`):

```text
https://jenkins.wirenboard.com/job/wirenboard/job/wb-mge/job/<branch-encoded>/
```

Store the credentials in a **`.env`** file at the repo root (it is gitignored) so
the agent can `source` it instead of being handed the token in chat:

```sh
# .env — never commit
JENKINS_USER=you.copr.name@wirenboard.com
JENKINS_TOKEN=11xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

Load it before running curl:

```bash
set -a; . ./.env; set +a
AUTH="$JENKINS_USER:$JENKINS_TOKEN"
```

Useful curl recipes (`$JOB` = the base URL above):

```bash
# List recent builds (number, result, timestamp, duration)
curl -s -u "$AUTH" "$JOB/api/json?tree=builds%5Bnumber,result,timestamp,duration%5D"

# Get the full console log for build #N
curl -s -u "$AUTH" "$JOB/<N>/logText/progressiveText?start=0"

# Status of a single build (still building? result?)
curl -s -u "$AUTH" "$JOB/<N>/api/json?tree=building,result,duration"
```

Stage semantics worth knowing: the `Lint` stage is wrapped in `catchError`, so
lint failures turn the build **UNSTABLE**, not FAILURE. QEMU/integration test
failures also mark builds UNSTABLE.
