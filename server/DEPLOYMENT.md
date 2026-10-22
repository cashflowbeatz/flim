# Cloudflare and private testing

## What the repository runs

This is a Node.js process with a WebSocket relay, HTTP sample transfers, in-memory sessions, and
a local disk cache. The supplied [`fly.toml`](fly.toml) targets Fly.io and the upstream public test
address in the root README is `flim.fly.dev`. There is no Worker entry point, Wrangler config,
Durable Object, or R2 backend in this repository. These are source findings; they do not establish
the current infrastructure of a separately deployed custom domain. No public session was contacted
for testing.

For a small group, the existing server can run on your PC or a VM behind Cloudflare Tunnel.
Cloudflare supplies the public HTTPS/WebSocket endpoint; your Node.js process still owns sessions
and sample storage. A Worker implementation would be separate work: move session ordering and
locks into Durable Objects, replace local sample storage with R2, and preserve the client wire
protocol. [Cloudflare's WebSocket documentation](https://developers.cloudflare.com/workers/runtime-apis/websockets/)
describes the Workers and Durable Objects model.

## Run a local origin for a tunnel

Use a supported Node.js LTS release; Node.js 24 is the container default. In PowerShell, from
the `server` directory:

```powershell
npm ci
npm test
$env:FLIM_PASSWORD = Read-Host 'Choose the server hosting password'
$env:FLIM_PROXY = 'cloudflare'
$env:FLIM_BIND = '127.0.0.1'
$env:PORT = '8080'
$env:FLIM_BLOB_FILE_MAX = '95000000'
npm start
```

The 95,000,000-byte cap is a conservative starting point below Cloudflare Free/Pro's documented
100 MB HTTP upload limit. Check your zone's limit and adjust it deliberately. Each sample currently
uses one PUT request; it is not split into chunks. HTTP size and timeout limits still apply through
a tunnel. Larger sample transfers require a different plan, a direct/private route, or a future
chunked/object-storage transfer design. See [Cloudflare upload limits](https://developers.cloudflare.com/cache/concepts/default-cache-behavior/#upload-limits).

Create a named tunnel using [Cloudflare's setup guide](https://developers.cloudflare.com/tunnel/get-started/).
Run its `cloudflared` connector on the **same machine** as flim-server. Publish a hostname such as
`flim.example.com` with service URL `http://127.0.0.1:8080`. Route the whole hostname so both
WebSockets and `/blob/…` / `/blobs/missing` reach this server. Keep the connector token private.
No router port-forward is needed for this arrangement.

The server trusts `CF-Connecting-IP` only in this mode and only from loopback. It requires a
loopback bind so public clients cannot connect directly and forge that header. A connector in a
separate container has a different network boundary; use a shared network namespace or adapt
the proxy trust policy explicitly instead of binding this mode to `0.0.0.0`.

Set a cache-bypass rule for `/blob/*` and `/blobs/*`, and preserve the server's `private, no-store`
response header. The sample authorization key is a header, not part of the URL. Do not apply a
cache-everything rule that overrides this header. Cloudflare's [cache rules](https://developers.cloudflare.com/cache/concepts/default-cache-behavior/)
describe when origin headers can be overridden.

The native client does not implement a browser login or Cloudflare Access service-token headers.
An interactive Access login in front of this hostname prevents it from connecting. Use flim's
hosting/session passwords for this public endpoint, or a separately configured private network
route for access restricted to your friends' devices. Cloudflare documents restrictions on serving
large files through public hostname routes; check the [Tunnel routing guidance](https://developers.cloudflare.com/tunnel/concepts/routing/)
before using it for substantial sample libraries.

## Check before inviting friends

1. Request `https://flim.example.com/health`; expect `flim-server ok`.
2. Host with Ctrl+click **Host**, server `flim.example.com`, and the server hosting password.
   Give friends the resulting session link and the separate session password.
3. Use the same verified FL build on both machines. Check a knob, notes, an arrangement change,
   and a small sample that the joining machine does not already have. Save a disposable project
   before testing disconnects.
4. Confirm that disconnecting the host closes the session, and that the old sample key no longer
   works. Start another session to confirm recovery.

Keep one server process and one origin replica: session maps and file authorization are local.
Cloudflare can close WebSockets during network updates, so a tunnel does not guarantee uninterrupted
sessions. flim sends ping frames every 15 seconds, but it has no server-side session recovery or
leader handoff. See [Cloudflare WebSocket behavior](https://developers.cloudflare.com/network/websockets/).

The repository tests exercise local protocol, isolation, quota, proxy-header, and malformed-input
behavior. They do not validate a Cloudflare account configuration, internet latency, or FL 26 hooks.
