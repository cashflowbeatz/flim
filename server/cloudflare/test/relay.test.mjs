import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { Miniflare } from "miniflare";

const owner = "local-test-host-key-never-deploy-this";
const compat = fl => ({ protocol: 1, fl, wire: "flim-25-26-v1" });
const sha = text => createHash("sha256").update(text).digest("hex");
async function setup(t, bindings = {}) {
  const options = {
    modules: true, scriptPath: fileURLToPath(new URL("fixture.mjs", import.meta.url)),
    compatibilityDate: "2026-07-30", durableObjects: { ROOM: { className: "TestRoom", useSQLite: true } },
    r2Buckets: ["FILES"], bindings: { HOST_KEY: owner, ...bindings }
  };
  const mf = new Miniflare(options);
  t.after(() => mf.dispose());
  const open = async () => {
    const response = await mf.dispatchFetch("http://flim/", { headers: { Upgrade: "websocket" } });
    assert.equal(response.status, 101);
    const ws = response.webSocket, queue = [], waits = [];
    ws.accept();
    ws.addEventListener("message", e => { const message = JSON.parse(e.data); const resolve = waits.shift(); if (resolve) resolve(message); else queue.push(message); });
    return {
      ws, send: message => ws.send(JSON.stringify(message)),
      async next(type) {
        for (;;) {
          const message = queue.length ? queue.shift() : await new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error("No message: " + type)), 5000);
            waits.push(value => { clearTimeout(timer); resolve(value); });
          });
          if (!type || message.t === type) return message;
        }
      }
    };
  };
  const hello = (type, id, fl = "25.2.5.5319", extra = {}) => ({ t: type, transport: 2, user: { id, name: id }, password: "room-password", owner, session: "01", compat: compat(fl), ...extra });
  const host = async (fl = "25.2.5.5319") => { const c = await open(); c.send(hello("host", "host", fl)); const welcome = await c.next("welcome"); return { ...c, welcome }; };
  const join = async (id = "member", fl = "26.1.7.5654") => { const c = await open(); c.send(hello("join", id, fl)); const welcome = await c.next("welcome"); return { ...c, welcome }; };
  const file = (key, hash, method = "GET", body) => mf.dispatchFetch("http://flim/blob/" + hash, { method, headers: { "x-flim-key": key, ...(body !== undefined ? { "content-length": String(Buffer.byteLength(body)) } : {}) }, ...(body !== undefined ? { body } : {}) });
  const namespace = await mf.getDurableObjectNamespace("ROOM");
  const room = namespace.get(namespace.idFromName("private-room-01"));
  const usage = () => mf.dispatchFetch("http://flim/usage", { headers: { "x-flim-owner": owner } }).then(r => r.json());
  return { mf, options, room, open, hello, host, join, file, usage };
}

test("monthly R2 cutoff charges failed attempts, but not duplicates, HEAD or missing queries", async t => {
  const { mf, host, file, usage } = await setup(t, { R2_MONTHLY_UPLOAD_LIMIT: "2", R2_MONTHLY_DOWNLOAD_LIMIT: "1" });
  const h = await host(), key = h.welcome.blob, body = "quota-body", hash = sha(body);
  assert.equal((await file(key, sha("wrong-hash"), "PUT", body)).status, 400);
  assert.equal((await file(key, hash, "PUT", body)).status, 201);
  assert.equal((await file(key, hash, "PUT", body)).status, 200);
  const blocked = await file(key, sha("blocked"), "PUT", "blocked");
  assert.equal(blocked.status, 429); assert.equal((await blocked.json()).code, "r2_monthly_limit");
  assert.equal((await file(key, hash, "HEAD")).status, 200);
  assert.equal((await mf.dispatchFetch("http://flim/blobs/missing", { method: "POST", headers: { "x-flim-key": key }, body: hash })).status, 200);
  assert.equal((await file(key, hash)).status, 200);
  assert.equal((await file(key, hash)).status, 429);
  assert.equal((await file(key, hash, "HEAD")).status, 200);
  const state = await usage(); assert.equal(state.uploads.used, 2); assert.equal(state.downloads.used, 1);
  assert.equal(state.uploads.blocked, true); assert.equal(state.downloads.blocked, true);
  assert.equal((await mf.dispatchFetch("http://flim/usage")).status, 403);
  assert.equal((await mf.dispatchFetch("http://flim/usage", { headers: { "x-flim-owner": "wrong" } })).status, 403);
});

test("concurrent uploads cannot overspend the final monthly reservation", async t => {
  const { host, file, usage } = await setup(t, { R2_MONTHLY_UPLOAD_LIMIT: "1" });
  const h = await host(), key = h.welcome.blob;
  const results = await Promise.all(["first", "second"].map(body => file(key, sha(body), "PUT", body)));
  assert.deepEqual(results.map(r => r.status).sort(), [201, 429]);
  const state = await usage(); assert.equal(state.uploads.used, 1); assert.equal(state.storage.files, 1);
});

test("stream length mismatch cannot exceed reserved bytes or leave an orphan", async t => {
  const { mf, room, host, usage } = await setup(t); const h = await host();
  for (const length of [2, 20]) assert.equal(await room.testBadLength(h.welcome.blob, sha("body"), "body", length), 400);
  const state = await usage(); assert.equal(state.storage.bytes, 0); assert.equal(state.storage.files, 0); assert.equal(state.uploads.used, 2);
  assert.equal((await (await mf.getR2Bucket("FILES")).list()).objects.length, 0);
});

test("zero monthly download limit disables reads without disabling metadata or free cleanup", async t => {
  const { room, host, file, usage } = await setup(t, { R2_MONTHLY_DOWNLOAD_LIMIT: "0" }); const h = await host();
  assert.equal((await file(h.welcome.blob, sha("data"), "PUT", "data")).status, 201);
  assert.equal((await file(h.welcome.blob, sha("data"))).status, 429);
  assert.equal((await file(h.welcome.blob, sha("data"), "HEAD")).status, 200);
  await room.testEndRoom(); await room.testAgeRoom();
  assert.equal((await usage()).storage.bytes, 0); assert.equal((await usage()).downloads.used, 0);
});

test("storage cutoff reserves in-flight bytes and rejects invalid limits closed", async t => {
  const { host, file, usage } = await setup(t, { R2_STORAGE_LIMIT_BYTES: "8" });
  const h = await host();
  const results = await Promise.all(["12345", "abcde"].map(body => file(h.welcome.blob, sha(body), "PUT", body)));
  assert.deepEqual(results.map(r => r.status).sort(), [201, 507]);
  assert.equal((await usage()).storage.bytes, 5);
  const other = await setup(t, { R2_MONTHLY_UPLOAD_LIMIT: "typo" }); const h2 = await other.host();
  assert.equal((await other.file(h2.welcome.blob, sha("a"), "PUT", "a")).status, 429);
  assert.equal((await other.usage()).uploads.limit, 0);
});

test("monthly usage survives session cleanup and worker restart; old periods do not consume a new month", async t => {
  const { mf, options, room, host, file, usage } = await setup(t, { R2_MONTHLY_UPLOAD_LIMIT: "1" });
  const h = await host();
  await room.testR2Budget("uploads", 20000, "2000-01");
  assert.equal((await file(h.welcome.blob, sha("counted"), "PUT", "counted")).status, 201);
  await room.testEndRoom(); await room.testAgeRoom();
  assert.equal((await usage()).storage.bytes, 0); assert.equal((await usage()).uploads.used, 1);
  await mf.setOptions(options);
  const state = await usage(); assert.equal(state.uploads.used, 1); assert.equal(state.uploads.blocked, true);
  const h2 = await host(); assert.equal((await file(h2.welcome.blob, sha("new-room"), "PUT", "new-room")).status, 429);
});

for (const loseReservation of [false, true]) test(`upload cleanup race: ${loseReservation ? "lost reservation fails closed" : "active stream retains reservation"}`, async t => {
  const { mf, room, host, file, usage } = await setup(t); const h = await host();
  const body = "long-running-upload", hash = sha(body); let controller;
  const stream = new ReadableStream({ start(c) { controller = c; c.enqueue(Buffer.from(body.slice(0, 4))); } });
  const pending = mf.dispatchFetch("http://flim/blob/" + hash, { method: "PUT", headers: { "x-flim-key": h.welcome.blob, "content-length": String(body.length) }, body: stream, duplex: "half" });
  try {
    const deadline = Date.now() + 3000;
    while (!(await room.testPending()) && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 10));
    assert.equal(await room.testPending(), 1);
    if (loseReservation) await room.testLoseReservation();
    else { await room.testAgeUploads(); assert.equal(await room.testPending(), 1); }
  } finally { controller.enqueue(Buffer.from(body.slice(4))); controller.close(); }
  assert.equal((await pending).status, loseReservation ? 400 : 201);
  assert.equal((await file(h.welcome.blob, hash)).status, loseReservation ? 404 : 200);
  const state = await usage(); assert.equal(state.storage.bytes, loseReservation ? 0 : body.length);
  const bucket = await mf.getR2Bucket("FILES");
  assert.equal((await bucket.list()).objects.length, loseReservation ? 0 : 1);
});

test("handshake errors distinguish file transport, connection metadata and unsupported FL builds", async t => {
  const { open, hello } = await setup(t);
  const cases = [
    ...[undefined, 1, "2"].map(transport => ({ extra: { transport }, message: /unsupported file-transfer protocol/ })),
    ...[null, [], {}, { protocol: 1, wire: "flim-25-26-v1" },
      { ...compat("25.2.5.5319"), protocol: 2 }, { ...compat("25.2.5.5319"), wire: "unknown" },
      { ...compat("25.2.5.5319"), extra: true }, compat(null), compat("025.2.5.5319"),
      compat("25.2.5.65536")].map(value => ({ extra: { compat: value }, message: /invalid or incompatible connection details/ })),
    ...["24.2.2.4596", "24.2.2.4598", "27.0.0.1"].map(fl => ({ extra: { compat: compat(fl) }, message: /does not support this FL Studio build/ }))
  ];
  for (const { extra, message } of cases) {
    const c = await open(); c.send(hello("join", "rejected", "25.2.5.5319", { user: null, ...extra }));
    const response = await c.next();
    assert.equal(response.t, "error"); assert.equal(response.code, "compatibility");
    assert.match(response.msg, message); assert.match(response.msg, /Cloudflare runs on the relay; no separate installation is needed/);
    assert.ok(response.msg.length < 240, "The complete reason must fit the native compatibility alert.");
  }
});

test("known FL24, FL25 and FL26 handshake identities reach user validation unchanged", async t => {
  const { open, hello } = await setup(t);
  for (const fl of ["24.2.2.4597", "25.2.5.5319", "26.1.7.5654"]) {
    const c = await open(); c.send(hello("join", "invalid-user", fl, { user: null }));
    const response = await c.next();
    assert.equal(response.t, "error"); assert.equal(response.code, "bad_request");
    assert.equal(response.msg, "Invalid user or session password.");
  }
});

test("hosting key, password, transport and oldest-build admission precede welcome", async t => {
  const { open, hello, host } = await setup(t);
  for (const extra of [{ owner: "wrong" }, { transport: 1 }, { compat: compat("27.0.0.1") }]) {
    const c = await open(); c.send(hello("host", "bad", undefined, extra)); assert.equal((await c.next()).t, "error");
  }
  const h = await host(); assert.equal(h.welcome.ends_in, 0); assert.equal(h.welcome.transport, 2); assert.equal(h.welcome.session, "01");
  for (const extra of [{ password: "wrong" }, { compat: compat("25.1.0.1") }, { transport: undefined }]) {
    const c = await open(); c.send(hello("join", "bad", undefined, extra)); assert.equal((await c.next()).t, "error");
  }
});

test("mixed sessions preserve sender build and enforce operation policy", async t => {
  const { host, join } = await setup(t); const h = await host(), m = await join();
  m.send({ t: "op", op: { kind: "notes.set", value: 2 } });
  const op = await h.next("op"); assert.equal(op.source_fl, "26.1.7.5654"); assert.equal(op.from, "member"); assert.ok(op.seq > 0);
  for (const kind of ["project.load", "plug.full", "notes.full", "state.full", "plug.state", "chan.add", "unknown"]) {
    m.send({ t: "op", op: { kind } }); assert.equal((await m.next("error")).code, "compatibility");
  }
  h.send({ t: "op", op: { kind: "plug.state", data: "" } }); assert.equal((await m.next("op")).source_fl, "25.2.5.5319");
  m.send({ t: "op", op: { kind: "notes.set", hidden: { t: "project_swap" } } }); assert.equal((await m.next("error")).code, "bad_request");
});

test("exact FL24 hosts newer peers while adjacent unknown builds and newer opaque state are refused", async t => {
  const { host, join, open, hello } = await setup(t);
  const h = await host("24.2.2.4597");
  assert.equal(h.welcome.compatibility.host_fl, "24.2.2.4597");
  for (const [id, fl] of [["fl25", "25.2.5.5319"], ["fl26", "26.1.7.5654"]]) {
    const m = await join(id, fl);
    assert.equal(m.welcome.compatibility.mixed, true);
    for (const kind of ["notes.set", "clips.set", "meta.set", "param.set"]) {
      m.send({ t: "op", op: { kind } });
      assert.equal((await h.next("op")).source_fl, fl);
    }
    m.send({ t: "op", op: { kind: "plug.state" } });
    assert.equal((await m.next("error")).code, "compatibility");
  }
  for (const fl of ["24.2.2.4596", "24.2.2.4598"]) {
    const bad = await open(); bad.send(hello("join", fl, fl));
    assert.equal((await bad.next("error")).code, "compatibility");
  }
});

const versionMatrix = ["24.2.2.4597", "25.2.5.5319", "26.1.7.5654"];
for (const [hi, hostBuild] of versionMatrix.entries()) for (const [mi, memberBuild] of versionMatrix.entries()) {
  test(`portable host order: ${hostBuild} host, ${memberBuild} member`, async t => {
    const {open, hello} = await setup(t);
    const h = await open(); h.send(hello('host', 'host', hostBuild, {portable_sync:1}));
    assert.equal((await h.next('welcome')).portable_sync, 1);
    const m = await open(); m.send(hello('join', 'member', memberBuild, {portable_sync:1}));
    assert.equal((await m.next('welcome')).portable_sync, 1);
    for (const [sender, recipient, source] of [[h,m,hostBuild],[m,h,memberBuild]]) {
      sender.send({t:'op',op:{kind:'notes.set',pat:1,data:'',marker:source}});
      assert.equal((await recipient.next('op')).op.marker,source);
      sender.send({t:'op',op:{kind:'plug.state',unit:'ch',ch:0,data:'',marker:source}});
      assert.equal((await recipient.next('op')).source_fl,source);
    }
    h.send({t:'op',op:{kind:'project.load',data:'newer-project-must-not-enter-older-loader'}});
    h.send({t:'op',op:{kind:'param.set',id:0,v:42}});
    const first=await m.next('op');
    assert.equal(first.op.kind,mi < hi ? 'param.set' : 'project.load');
    if(mi >= hi) assert.equal((await m.next('op')).op.kind,'param.set');
    m.send({t:'op',op:{kind:'portable.start',nch:1,for:'host'}});
    assert.equal((await m.next('error')).code,'compatibility');
  });
}

test('portable room requires the matching client and keeps its mode after restart',async t=>{
  const {open,hello,room}=await setup(t);
  const h=await open();h.send(hello('host','host','26.1.7.5654',{portable_sync:1}));await h.next('welcome');
  const legacy=await open();legacy.send(hello('join','legacy','26.1.7.5654'));
  assert.match((await legacy.next('error')).msg,/current flim app/);
  assert.deepEqual(await room.testSavedPortableMode(),[{code:'01',portable:true}]);
  for(const portable_sync of [0,2,'1',true,null,{}]){
    const c=await open();c.send(hello('join','bad','24.2.2.4597',{portable_sync}));
    assert.equal((await c.next('error')).code,'compatibility');
  }
});

test('targeted portable baselines and stored payloads reach only their intended participant',async t=>{
  const {open,hello,file}=await setup(t);
  const h=await open();h.send(hello('host','host','26.1.7.5654',{portable_sync:1}));const welcome=await h.next('welcome');
  const a=await open();a.send(hello('join','a','24.2.2.4597',{portable_sync:1}));await a.next('welcome');
  const b=await open();b.send(hello('join','b','25.2.5.5319',{portable_sync:1}));await b.next('welcome');
  h.send({t:'op',op:{kind:'portable.start',for:'a',nch:4}});
  const body=JSON.stringify({t:'op',op:{kind:'notes.full',for:'a',items:[]}}), hash=sha(body);
  assert.equal((await file(welcome.blob,hash,'PUT',body)).status,201);
  h.send({t:'stored',type:'op',kind:'notes.full',for:'a',h:hash,s:body.length});
  h.send({t:'op',op:{kind:'param.set',id:0,v:73}});
  assert.equal((await a.next('op')).op.kind,'portable.start');
  const stored=await a.next('stored');assert.equal(stored.for,'a');assert.equal(stored.source_fl,'26.1.7.5654');
  assert.equal((await a.next('op')).op.v,73);
  assert.equal((await b.next('op')).op.kind,'param.set');
  b.send({t:'stored',type:'op',kind:'notes.set',for:'a',h:hash,s:body.length});
  assert.equal((await b.next('error')).code,'compatibility');
  b.send({t:'op',op:{kind:'clips.set',arr:0,data:''}});
  assert.equal((await b.next('error')).code,'compatibility');
});
for (const [hi, hostBuild] of versionMatrix.entries()) for (const [mi, memberBuild] of versionMatrix.entries()) {
  test(`FL24/25/26 relay matrix: ${hostBuild} host, ${memberBuild} member`, async t => {
    const { host, join, open, hello } = await setup(t);
    const h = await host(hostBuild);
    if (mi < hi) {
      h.send({t: "project_swap", flp: "synthetic-project-only"});
      const rejected = await open(); rejected.send(hello("join", "older", memberBuild));
      const first = await rejected.next();
      assert.equal(first.t, "error"); assert.equal(first.code, "compatibility");
      return;
    }
    const m = await join("member", memberBuild);
    assert.equal(m.welcome.compatibility.host_fl, hostBuild);
    assert.equal(m.welcome.compatibility.mixed, mi !== hi);
    let previous = 0;
    for (const kind of ["notes.set", "clips.set", "meta.set", "param.set"]) {
      for (const [sender, recipient, from, source_fl] of [[m, h, "member", memberBuild], [h, m, "host", hostBuild]]) {
        const marker = `${kind}:${from}`;
        sender.send({t: "op", op: {kind, value: marker}});
        const received = await recipient.next("op");
        assert.equal(received.op.value, marker); assert.equal(received.op.kind, kind);
        assert.equal(received.source_fl, source_fl); assert.equal(received.from, from);
        assert.ok(received.seq > previous); previous = received.seq;
      }
    }
  });
}

test("FL24 cannot join a newer host before receiving project data", async t => {
  const { host, open, hello } = await setup(t);
  await host("26.1.7.5654");
  const older = await open(); older.send(hello("join", "older", "24.2.2.4597"));
  const response = await older.next();
  assert.equal(response.t, "error"); assert.equal(response.code, "compatibility");
});

test("locks deny conflicting edits, survive long keys and clear on project replacement", async t => {
  const { host, join } = await setup(t); const h = await host(), m = await join();
  h.send({ t: "lock_acquire", key: "chan:1" }); assert.equal((await h.next("lock")).granted, true);
  await m.next("lock");
  m.send({ t: "lock_acquire", key: "chan:1" }); assert.equal((await m.next("lock")).granted, false);
  m.send({ t: "op", op: { kind: "notes.set", key: "chan:1" } }); assert.equal((await m.next("error")).code, "locked");
  for (let i = 0; i < 8; i++) { h.send({ t: "lock_acquire", key: String(i).padEnd(256, "x") }); const result = await h.next(); if (result.t === "error") { assert.equal(result.code, "lock_limit"); break; } assert.equal(result.t, "lock"); }
  h.send({ t: "op", op: { kind: "project.load", data: "" } }); await m.next("op");
  m.send({ t: "lock_acquire", key: "chan:1" }); assert.equal((await m.next("lock")).granted, true);
});

test("R2 transfers match the native line protocol and verify content hashes", async t => {
  const { mf, host, file } = await setup(t); const h = await host(), key = h.welcome.blob;
  const body = "sample-content", hash = sha(body), missing = sha("missing");
  assert.equal((await file("bad", hash, "PUT", body)).status, 403);
  assert.equal((await file(key, missing, "PUT", body)).status, 400);
  assert.equal((await file(key, hash, "PUT", body)).status, 201);
  assert.equal((await file(key, hash, "PUT", body)).status, 200);
  assert.equal(await (await file(key, hash)).text(), body);
  const head = await file(key, hash, "HEAD"); assert.equal(head.headers.get("content-length"), String(body.length));
  const response = await mf.dispatchFetch("http://flim/blobs/missing", { method: "POST", headers: { "x-flim-key": key }, body: hash + "\n" + missing + "\n" + missing + "\n" });
  assert.equal(response.status, 200); assert.equal(await response.text(), missing);
  assert.equal((await mf.dispatchFetch("http://flim/blob/" + missing, { method: "PUT", headers: { "x-flim-key": key } })).status, 413);
});

test("stored edits retain policy, sequence and metadata without oversized WebSocket frames", async t => {
  const { host, join, file } = await setup(t); const h = await host(), m = await join();
  const body = JSON.stringify({ t: "op", op: { kind: "project.load", data: "A".repeat(180000) } }), hash = sha(body);
  const ref = { t: "stored", type: "op", kind: "project.load", h: hash, s: body.length };
  h.send(ref); assert.equal((await h.next("error")).code, "bad_request");
  assert.equal((await file(h.welcome.blob, hash, "PUT", body)).status, 201);
  m.send(ref); assert.equal((await m.next("error")).code, "compatibility");
  h.send({ ...ref, s: body.length + 1 }); assert.equal((await h.next("error")).code, "bad_request");
  h.send({ ...ref, source_fl: "25.0.0.1" }); assert.equal((await h.next("error")).code, "bad_request");
  h.send(ref); const got = await m.next("stored"); assert.equal(got.h, hash); assert.equal(got.source_fl, "25.2.5.5319"); assert.ok(got.seq > 0);
  assert.equal(await (await file(m.welcome.blob, got.h)).text(), body);
});

for (const hostFl of ['24.2.2.4597', '25.2.5.5319', '26.1.7.5654']) test(`CRDT handshake and bidirectional deltas with FL${hostFl} host`, async t => {
  const {open, hello, file} = await setup(t);
  const h = await open(); h.send(hello('host','host',hostFl,{notes_crdt:1}));
  const welcome = await h.next('welcome'); assert.equal(welcome.notes_crdt, 1);
  const m = await open(); m.send(hello('join','member','26.1.7.5654',{notes_crdt:1}));
  assert.equal((await m.next('welcome')).notes_crdt, 1);
  assert.equal((await h.next('presence')).notes_crdt, 1);
  const op = {kind:'notes.set',pat:1,crdt:1,data:Buffer.from('FLIMNC01').toString('base64')};
  for (const [sender, receiver, fl] of [[m,h,'26.1.7.5654'],[h,m,hostFl]]) {
    sender.send({t:'op',op}); const got = await receiver.next('op');
    assert.deepEqual(got.op, op); assert.equal(got.source_fl, fl); assert.equal(got.op.key, undefined);
  }
  m.send({t:'op',op:{kind:'notes.set',pat:1,data:''}});
  assert.equal((await m.next('error')).code,'compatibility');
  h.send({t:'op',op:{kind:'notes.full',items:[{pat:1,data:''}]}});
  assert.equal((await h.next('error')).code,'compatibility');
  h.send({t:'op',op:{kind:'notes.full',items:[{pat:1,crdt:1,data:''}]}});
  assert.equal((await m.next('op')).op.items[0].crdt,1);
  const body=JSON.stringify({t:'op',op:{...op,data:'A'.repeat(180000)}}), hash=sha(body);
  assert.equal((await file(welcome.blob,hash,'PUT',body)).status,201);
  m.send({t:'stored',type:'op',kind:'notes.set',h:hash,s:body.length});
  const stored=await h.next('stored'); assert.equal(stored.kind,'notes.set');
  assert.equal(await (await file(welcome.blob, stored.h)).text(),body);
});

test('CRDT room refuses an older client before sending project state and persists its mode', async t => {
  const {room,open,hello} = await setup(t);
  const h=await open(); h.send(hello('host','host',undefined,{notes_crdt:1})); await h.next('welcome');
  const old=await open(); old.send(hello('join','old'));
  const error=await old.next(); assert.equal(error.t,'error'); assert.equal(error.code,'compatibility'); assert.match(error.msg,/current flim app/);
  assert.deepEqual(await room.testSavedNoteMode(), [{code:'01',crdt:true}]);
  const m=await open(); m.send(hello('join','new','26.1.7.5654',{notes_crdt:1}));
  assert.equal((await m.next('welcome')).notes_crdt,1);
});

test('a CRDT capable client falls back when joining a legacy room', async t => {
  const {host,open,hello}=await setup(t); const h=await host(); assert.equal(h.welcome.notes_crdt,undefined);
  const m=await open(); m.send(hello('join','new','26.1.7.5654',{notes_crdt:1}));
  assert.equal((await m.next('welcome')).notes_crdt,undefined);
  assert.equal((await h.next('presence')).notes_crdt,undefined);
  m.send({t:'op',op:{kind:'notes.set',pat:1,crdt:1,data:''}}); assert.equal((await m.next('error')).code,'compatibility');
  m.send({t:'op',op:{kind:'notes.set',pat:1,data:''}}); assert.equal((await h.next('op')).op.crdt,undefined);
});

test('unsupported note merge offers fail before room admission', async t => {
  const {open,hello}=await setup(t);
  for(const notes_crdt of [0,2,'1',null,true,{}]) {
    const c=await open(); c.send(hello('host','bad',undefined,{notes_crdt}));
    const result=await c.next(); assert.equal(result.t,'error'); assert.equal(result.code,'compatibility');
  }
});

test("snapshot references persist for late joiners and session end revokes file access", async t => {
  const { host, join, file } = await setup(t); const h = await host();
  h.send({ t: "project_swap", flp: "project-data" });
  const m = await join(); const snapshot = await m.next("stored"); assert.equal(snapshot.delivery, "snapshot"); assert.equal(snapshot.epoch, 1);
  assert.deepEqual(JSON.parse(await (await file(h.welcome.blob, snapshot.h)).text()), { t: "project_swap", flp: "project-data" });
  h.ws.close(1000, "test done"); await m.next("leader_left");
  // A fresh host handshake is serialized after the room-end operation.
  const next = await host(); assert.notEqual(next.welcome.blob, h.welcome.blob);
  assert.equal((await file(h.welcome.blob, snapshot.h)).status, 403);
  assert.equal((await file(next.welcome.blob, snapshot.h)).status, 404);
});

test("private room allows eight people and rejects duplicate identities", async t => {
  const { host, join, open, hello } = await setup(t); await host(); await join("member1");
  const duplicate = await open(); duplicate.send(hello("join", "member1")); assert.equal((await duplicate.next("error")).code, "id_in_use");
  for (let i = 2; i < 8; i++) await join("member" + i);
  const full = await open(); full.send(hello("join", "overflow")); assert.equal((await full.next("error")).code, "session_full");
});

test("lock alarms expire abandoned locks without timing out an admitted session", async t => {
  const { room, host, join } = await setup(t); const h = await host(), m = await join();
  h.send({ t: "lock_acquire", key: "chan:1" }); await h.next("lock"); await m.next("lock");
  await room.testExpireLocks(); assert.equal((await m.next("lock_released")).key, "chan:1");
  await room.testAgeRoom();
  m.send({ t: "op", op: { kind: "notes.set" } }); assert.equal((await h.next("op")).op.kind, "notes.set");
});

test("application upload, download and aggregate-storage limits fail explicitly", async t => {
  const { room, host, file } = await setup(t); const h = await host(), key = h.welcome.blob;
  await room.testBudget("uploads", 1000); assert.equal((await file(key, sha("limited"), "PUT", "limited")).status, 429);
  await room.testBudget("uploads", 0); assert.equal((await file(key, sha("allowed"), "PUT", "allowed")).status, 201);
  await room.testBudget("reads", 5000); assert.equal((await file(key, sha("allowed"))).status, 429);
  await room.testStorageFull(); assert.equal((await file(key, sha("full"), "PUT", "full")).status, 507);
  assert.equal(await room.testPending(), 0);
});

test("disconnect completes its WebSocket close handshake", async t => {
  const { host, join } = await setup(t); await host(); const m = await join();
  const closed = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("Close handshake stalled")), 5000);
    m.ws.addEventListener("close", e => { clearTimeout(timer); resolve(e); });
  });
  m.ws.close(1000, "finished"); assert.equal((await closed).code, 1000);
});

test("session ending during a streamed upload cannot commit its file", async t => {
  const { mf, room, host, file } = await setup(t); const h = await host();
  const body = "race-content", hash = sha(body); let controller;
  const stream = new ReadableStream({ start(c) { controller = c; c.enqueue(Buffer.from(body.slice(0, 4))); } });
  const pending = mf.dispatchFetch("http://flim/blob/" + hash, { method: "PUT", headers: { "x-flim-key": h.welcome.blob, "content-length": String(body.length) }, body: stream, duplex: "half" });
  try {
    const deadline = Date.now() + 3000;
    while (!(await room.testPending()) && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 10));
    assert.equal(await room.testPending(), 1);
    await room.testEndRoom();
  } finally { controller.enqueue(Buffer.from(body.slice(4))); controller.close(); }
  assert.equal((await pending).status, 400); assert.equal(await room.testPending(), 0);
  const next = await host(); assert.equal((await file(next.welcome.blob, hash)).status, 404);
});

for (const large of [false, true]) test(`queued messages are bounded by ${large ? "bytes" : "count"} and disconnected edits are discarded`, async t => {
  const { room, host, join } = await setup(t);
  const h = await host(), flood = await join("flood"), healthy = await join("healthy");
  flood.send({ t: "lock_acquire", key: "chan:1" });
  await flood.next("lock"); await h.next("lock"); await healthy.next("lock");
  await room.testBlockMessages();
  try {
    const op = { t: "op", op: { kind: "notes.set", value: large ? "x".repeat(120000) : "discard" } };
    for (let i = 0; i < (large ? 18 : 2001); i++) flood.send(op);
    assert.equal((await flood.next("error")).code, "relay_busy");
    const pending = await room.testMessageQueue();
    assert.ok(pending.bytes <= 2 * 1024 * 1024);
    healthy.send({ t: "op", op: { kind: "notes.set", value: "live" } });
  } finally { await room.testResumeMessages(); }
  assert.equal((await h.next("op")).op.value, "live");
  healthy.send({ t: "lock_acquire", key: "chan:1" });
  const lock = await healthy.next("lock"); assert.equal(lock.granted, true); assert.equal(lock.owner, "healthy");
  const deadline = Date.now() + 3000;
  while ((await room.testMessageQueue()).peers && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 10));
  const empty = await room.testMessageQueue(); assert.equal(empty.bytes, 0); assert.equal(empty.peers, 0);
});

test("oversized frames are rejected before waiting behind another operation", async t => {
  const { room, host, join } = await setup(t); const h = await host(), m = await join();
  await room.testBlockMessages();
  try {
    m.ws.send("x".repeat(128 * 1024 + 1));
    assert.equal((await m.next("error")).code, "bad_request");
    assert.equal((await room.testMessageQueue()).bytes, 0);
  } finally { await room.testResumeMessages(); }
  const replacement = await join("replacement");
  replacement.send({ t: "op", op: { kind: "notes.set", value: "recovered" } });
  assert.equal((await h.next("op")).op.value, "recovered");
});

test("live uploads cannot hide abandoned files behind the cleanup batch", async t => {
  const { mf, room, usage } = await setup(t);
  assert.equal(await room.testCleanupBehindUploads(), 64);
  assert.equal((await (await mf.getR2Bucket("FILES")).list()).objects.length, 0);
  assert.equal((await usage()).storage.files, 64);
});

test("many peers cannot exhaust memory with tiny pending messages", async t => {
  const { room, host, join } = await setup(t); await host();
  const members = [];
  for (let i = 0; i < 5; i++) members.push(await join("burst" + i));
  await room.testBlockMessages();
  try {
    for (let i = 0; i < 4; i++) for (let n = 0; n < 2000; n++) members[i].send({ t: "op", op: { kind: "cur" } });
    const deadline = Date.now() + 3000;
    while ((await room.testMessageQueue()).count < 8000 && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 10));
    assert.equal((await room.testMessageQueue()).count, 8000);
    members[4].send({ t: "op", op: { kind: "cur" } });
    assert.equal((await members[4].next("error")).code, "relay_busy");
    assert.equal((await room.testMessageQueue()).count, 8000);
    for (const member of members.slice(0, 4)) member.ws.close(1000, "end burst");
  } finally { await room.testResumeMessages(); }
  const deadline = Date.now() + 3000;
  while ((await room.testMessageQueue()).count && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 10));
  const empty = await room.testMessageQueue(); assert.equal(empty.count, 0); assert.equal(empty.bytes, 0); assert.equal(empty.peers, 0);
  const next = await join("recovered"); assert.equal(next.welcome.you.id, "recovered");
});
