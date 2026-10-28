import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";
import { Miniflare } from "miniflare";

const owner = "local-test-host-key-never-deploy-this";
const compat = { protocol: 1, fl: "26.1.7.5654", wire: "flim-25-26-v1" };
const sha = text => createHash("sha256").update(text).digest("hex");
async function setup(t, bindings = {}) {
  const options = {
    modules: true, scriptPath: fileURLToPath(new URL("fixture.mjs", import.meta.url)), compatibilityDate: "2026-07-30",
    durableObjects: { ROOM: { className: "TestRoom", useSQLite: true } }, r2Buckets: ["FILES"], bindings: { HOST_KEY: owner, ...bindings }
  };
  const mf = new Miniflare(options); t.after(() => mf.dispose());
  const namespace = await mf.getDurableObjectNamespace("ROOM"), state = namespace.get(namespace.idFromName("private-room-01"));
  const api = async (path, body = {}, ip = "test") => {
    const response = await mf.dispatchFetch("http://flim" + path, { method: "POST", headers: { "content-type": "application/json", "CF-Connecting-IP": ip }, body: JSON.stringify(body) });
    return { status: response.status, data: await response.json() };
  };
  const create = async () => { const result = await api("/rooms"); assert.equal(result.status, 201); return result.data; };
  const open = async (room, role, id, overrides = {}, ip = "test") => {
    const response = await mf.dispatchFetch("http://flim" + (room.room === "01" ? "/" : "/room/" + room.room), { headers: { Upgrade: "websocket", "CF-Connecting-IP": ip } });
    assert.equal(response.status, 101); const ws = response.webSocket, messages = [], waits = [];
    ws.accept(); ws.addEventListener("message", e => { const value = JSON.parse(e.data); messages.push(value); for (const done of waits.splice(0)) done(); });
    const client = { ws, messages, send: message => ws.send(JSON.stringify(message)), async next(type) {
      const until = Date.now() + 5000;
      for (;;) {
        const index = messages.findIndex(m => !type || m.t === type);
        if (index >= 0) return messages.splice(index, 1)[0];
        if (Date.now() > until) throw new Error("No message: " + type);
        await new Promise(resolve => { const timer = setTimeout(resolve, 50); waits.push(() => { clearTimeout(timer); resolve(); }); });
      }
    }};
    client.send({ t: role, user: { id, name: id }, owner: room.hostToken, password: room.password, session: room.room, compat, transport: 2, ...overrides });
    return client;
  };
  const host = async room => { const c = await open(room, "host", "host"); c.welcome = await c.next("welcome"); await c.next("lock_state"); c.messages.length = 0; return c; };
  const join = async (room, id = "member") => { const c = await open(room, "join", id); c.welcome = await c.next("welcome"); await c.next("lock_state"); c.messages.length = 0; return c; };
  const file = (key, body, method = "PUT") => mf.dispatchFetch("http://flim/blob/" + sha(body), { method, headers: { "x-flim-key": key, ...(method === "PUT" ? { "content-length": String(Buffer.byteLength(body)) } : {}) }, ...(method === "PUT" ? { body } : {}) });
  const usage = () => mf.dispatchFetch("http://flim/usage", { headers: { "x-flim-owner": owner } }).then(r => r.json());
  return { mf, options, state, api, create, open, host, join, file, usage };
}

test("anonymous room tickets are scoped, hashed, single use, and cannot change the issued password", async t => {
  const { create, api, open, host, state } = await setup(t), a = await create(), b = await create();
  assert.match(a.room, /^\d{8}$/); assert.notEqual(a.room, b.room); assert.equal(a.maxParticipants, 8);
  assert.equal(a.websocket, "/room/" + a.room); assert.equal(await state.testReservationToken(a.room), sha(a.hostToken));
  assert.equal((await api("/rooms/join", { room: a.room })).data.code, "room_not_ready");
  assert.equal((await api("/rooms/status", { room: a.room, password: "wrong" })).status, 404);
  assert.equal((await api("/rooms/status", a)).data.state, "waiting");
  for (const overrides of [{ owner: b.hostToken }, { password: "changed-password" }, { owner }]) {
    const invalid = await open(a, "host", "bad", overrides); assert.equal((await invalid.next("error")).code, "bad_owner");
  }
  const h = await host(a); assert.equal(h.welcome.session, a.room);
  const joined = await api("/rooms/join", { room: a.room }); assert.equal(joined.status, 200); assert.equal(joined.data.password, a.password); assert.equal(joined.data.hostToken, undefined);
  const duplicate = await open(a, "host", "duplicate"); assert.equal((await duplicate.next("error")).code, "bad_owner");
});

test("rooms isolate peers, edits, locks, snapshots and blob access; ending one leaves another active", async t => {
  const { create, api, host, join, file, state } = await setup(t), a = await create(), b = await create();
  const ah = await host(a), bh = await host(b), am = await join(a), bm = await join(b);
  ah.messages.length = bh.messages.length = 0;
  am.send({ t: "lock_acquire", key: "notes:1" }); assert.equal((await am.next("lock")).granted, true);
  bm.send({ t: "lock_acquire", key: "notes:1" }); assert.equal((await bm.next("lock")).granted, true);
  am.send({ t: "op", op: { kind: "notes.set", key: "notes:1", marker: "room-a" } });
  assert.equal((await ah.next("op")).op.marker, "room-a");
  bm.send({ t: "op", op: { kind: "notes.set", key: "notes:1", marker: "room-b" } });
  assert.equal((await bh.next("op")).op.marker, "room-b");
  assert.equal(ah.messages.some(m => m.t === "op"), false); assert.equal(bh.messages.some(m => m.t === "op"), false);
  assert.equal((await file(ah.welcome.blob, "only-a")).status, 201);
  assert.equal((await file(bh.welcome.blob, "only-a", "GET")).status, 404);
  ah.send({ t: "snapshot", flp: "a-project" }); await am.next("stored");
  assert.equal(bm.messages.some(m => m.t === "stored"), false);
  const status = await api("/rooms/status", a); assert.equal(status.data.state, "active"); assert.deepEqual(status.data.peers.map(p => p.id).sort(), ["host", "member"]);
  await state.testEndRoom(a.room); await state.testAgeRoom();
  assert.equal((await file(ah.welcome.blob, "only-a", "GET")).status, 403);
  assert.equal((await api("/rooms/join", { room: a.room })).status, 404);
  assert.equal((await api("/rooms/status", b)).data.state, "active");
  bm.send({ t: "op", op: { kind: "cur", marker: "still-live" } }); assert.equal((await bh.next("op")).op.marker, "still-live");
});

test("four room capacity includes pending reservations and running rooms; expiration frees capacity", async t => {
  const { create, api, state, open, host } = await setup(t); const reservations = [];
  for (let i = 0; i < 4; i++) reservations.push(await create());
  assert.equal((await api("/rooms")).data.code, "relay_full");
  const active = await host(reservations[0]);
  await state.testExpireReservations();
  assert.equal((await api("/rooms/status", reservations[0])).data.state, "active");
  assert.equal((await api("/rooms/join", { room: reservations[1].room })).status, 404);
  const stale = await open(reservations[1], "host", "stale"); assert.equal((await stale.next("error")).code, "bad_owner");
  await create(); assert.ok(active.welcome.blob);
});

test("only the pending room host can release capacity; active rooms and other reservations survive", async t => {
  const { create, api, host, open } = await setup(t);
  const a = await create(), b = await create(), c = await create(), d = await create();
  assert.equal((await api('/rooms')).data.code, 'relay_full');
  for (const body of [{room:a.room}, {room:a.room,password:a.password}, {room:a.room,hostToken:b.hostToken}])
    assert.equal((await api('/rooms/cancel',body)).status,404);
  assert.equal((await api('/rooms/status',a)).data.state,'waiting');
  assert.deepEqual((await api('/rooms/cancel',a)).data,{released:true});
  assert.equal((await api('/rooms/status',a)).status,404);
  const e = await create();
  assert.equal((await api('/rooms/cancel',a)).status,404);
  assert.equal((await api('/rooms/status',e)).data.state,'waiting');
  const stale = await open(a,'host','stale'); assert.equal((await stale.next('error')).code,'bad_owner');
  const active = await host(b);
  assert.equal((await api('/rooms/cancel',b)).status,404);
  assert.equal((await api('/rooms/status',b)).data.state,'active');
  for(const room of [c,d]) assert.equal((await api('/rooms/status',room)).data.state,'waiting');
  assert.ok(active.welcome.blob);
});

test("a host-handshake/cancel race either releases the reservation or preserves the active host", async t => {
  const { create, api, open } = await setup(t);
  const a = await create();
  const [client,cancel] = await Promise.all([open(a,'host','host'),api('/rooms/cancel',a)]);
  const outcome = await client.next();
  if(cancel.status === 200) {
    assert.equal(outcome.t,'error'); assert.equal(outcome.code,'bad_owner');
    assert.equal((await api('/rooms/status',a)).status,404);
  } else {
    assert.equal(cancel.status,404); assert.equal(outcome.t,'welcome');
    assert.equal((await api('/rooms/status',a)).data.state,'active');
  }
});

test("reservations and lookup limits survive restart while retired daily creation counters no longer block", async t => {
  const { mf, options, create, api, host, state: original } = await setup(t); const a = await create();
  await original.testBudget("hosts", 20);
  await original.testBudget("api:create", 100);
  await original.testBudget("api:create:" + sha("test"), 20);
  await mf.setOptions(options); assert.equal((await api("/rooms/status", a)).data.state, "waiting");
  const namespace = await mf.getDurableObjectNamespace("ROOM"), state = namespace.get(namespace.idFromName("private-room-01"));
  await host(a); await create();
  await state.testBudget("api:join", 2000); assert.equal((await api("/rooms/join", { room: a.room }, "different-ip")).status, 429);
  await state.testBudget("api:join", 0); await state.testBudget("api:join:" + sha("limited-ip"), 100);
  assert.equal((await api("/rooms/join", { room: a.room }, "limited-ip")).status, 429);
  assert.equal((await api("/rooms/join", { room: a.room }, "another-ip")).status, 200);
});

test("more than twenty rooms can be created in one UTC day when capacity is released", async t => {
  const { api } = await setup(t);
  for (let i = 0; i < 25; i++) {
    const created = await api("/rooms", {}, "creator-" + i);
    assert.equal(created.status, 201);
    assert.deepEqual((await api("/rooms/cancel", created.data)).data, { released: true });
  }
});

test("creation burst limits persist across restart, ignore expired minutes and leave other IPs available", async t => {
  const { mf, options, state, api } = await setup(t);
  await state.testBudget("api:create:" + sha("limited-ip"), 10, "minute");
  const limited = await api("/rooms", {}, "limited-ip");
  assert.equal(limited.status, 429); assert.equal(limited.data.code, "room_rate_limit");
  assert.match(limited.data.error, /minute/);
  assert.equal((await api("/rooms", {}, "another-ip")).status, 201);
  await state.testBudget("api:create", 60, "minute");
  await mf.setOptions(options);
  assert.equal((await api("/rooms", {}, "fresh-ip")).status, 429);
  const namespace = await mf.getDurableObjectNamespace("ROOM"), restored = namespace.get(namespace.idFromName("private-room-01"));
  await restored.testBudget("api:create", 0, "minute");
  await restored.testBudget("api:create", 60, "minute", Date.now() - 60_000);
  await restored.testBudget("api:create:" + sha("fresh-ip"), 10, "minute", Date.now() - 60_000);
  assert.equal((await api("/rooms", {}, "fresh-ip")).status, 201);
});

test("all rooms share one monthly R2 counter, including concurrent uploads and after one room closes", async t => {
  const { create, host, file, usage, state } = await setup(t, { R2_MONTHLY_UPLOAD_LIMIT: "1", R2_MONTHLY_DOWNLOAD_LIMIT: "1" });
  const a = await create(), b = await create(), ah = await host(a), bh = await host(b);
  const results = await Promise.all([file(ah.welcome.blob, "one"), file(bh.welcome.blob, "two")]);
  assert.deepEqual(results.map(r => r.status).sort(), [201, 429]); assert.equal((await usage()).uploads.used, 1);
  const winner = results[0].status === 201 ? ah : bh, body = results[0].status === 201 ? "one" : "two";
  assert.equal((await file(winner.welcome.blob, body, "GET")).status, 200);
  assert.equal((await file(winner.welcome.blob, body, "GET")).status, 429);
  await state.testEndRoom(a.room); await state.testAgeRoom();
  assert.equal((await file(bh.welcome.blob, "three")).status, 429); assert.equal((await usage()).downloads.used, 1);
});

test("global storage reservation covers every room and cleanup does not delete another room's files", async t => {
  const { create, host, file, usage, state } = await setup(t, { R2_STORAGE_LIMIT_BYTES: "10" });
  const a = await create(), b = await create(), ah = await host(a), bh = await host(b);
  assert.equal((await file(ah.welcome.blob, "aaaaa")).status, 201); assert.equal((await file(bh.welcome.blob, "bbbbb")).status, 201);
  assert.equal((await file(bh.welcome.blob, "c")).status, 507); assert.equal((await usage()).storage.bytes, 10);
  await state.testEndRoom(a.room); await state.testAgeRoom();
  assert.equal((await usage()).storage.bytes, 5); assert.equal((await file(bh.welcome.blob, "bbbbb", "GET")).status, 200);
  assert.equal((await file(bh.welcome.blob, "ccccc")).status, 201);
});


test("legacy room shares the four-slot cap without exposing private room membership through the app API", async t => {
  const { create, host, api, state } = await setup(t);
  await state.testBudget("hosts", 20);
  await host({ room: "01", password: "legacy-password", hostToken: owner });
  for (let i = 0; i < 3; i++) await create();
  assert.equal((await api("/rooms")).data.code, "relay_full");
  assert.equal((await api("/rooms/join", { room: "01" })).status, 400);
});

test("ending one room cannot abort an in-flight upload owned by another room", async t => {
  const { mf, create, host, file, state } = await setup(t);
  const a = await create(), b = await create(); await host(a); const bh = await host(b);
  const body = "another-room-upload"; let controller;
  const stream = new ReadableStream({ start(c) { controller = c; c.enqueue(Buffer.from(body.slice(0, 4))); } });
  const pending = mf.dispatchFetch("http://flim/blob/" + sha(body), { method: "PUT", headers: { "x-flim-key": bh.welcome.blob, "content-length": String(body.length) }, body: stream, duplex: "half" });
  try {
    const until = Date.now() + 3000;
    while (!(await state.testPending()) && Date.now() < until) await new Promise(resolve => setTimeout(resolve, 10));
    assert.equal(await state.testPending(), 1); await state.testEndRoom(a.room);
  } finally { controller.enqueue(Buffer.from(body.slice(4))); controller.close(); }
  assert.equal((await pending).status, 201); assert.equal((await file(bh.welcome.blob, body, "GET")).status, 200);
});


test("unknown WebSocket room guesses consume persistent per-IP and global admission budgets", async t => {
  const { open, state } = await setup(t);
  const missing = { room: "11111111", password: "unknown-room-password" };
  await state.testBudget("ws:admissions:" + sha("limited"), 199);
  const first = await open(missing, "join", "guess-1", {}, "limited"); assert.equal((await first.next("error")).code, "no_session");
  const blocked = await open(missing, "join", "guess-2", {}, "limited"); assert.equal((await blocked.next("error")).code, "rate_limit");
  const independent = await open(missing, "join", "guess-3", {}, "another"); assert.equal((await independent.next("error")).code, "no_session");
  await state.testBudget("ws:admissions", 4000);
  const globallyBlocked = await open(missing, "join", "guess-4", {}, "new-ip"); assert.equal((await globallyBlocked.next("error")).code, "rate_limit");
});

test("a missing R2 object releases stale metadata so its sender can upload it again", async t => {
  const {mf,create,host,file,usage}=await setup(t);
  const room=await create(), h=await host(room), body='recover-this-sample';
  assert.equal((await file(h.welcome.blob,body)).status,201);
  const bucket=await mf.getR2Bucket('FILES');
  const objects=(await bucket.list()).objects;
  assert.equal(objects.length,1);await bucket.delete(objects[0].key);
  assert.equal((await usage()).storage.files,1);
  assert.equal((await file(h.welcome.blob,body,'GET')).status,404);
  assert.equal((await usage()).storage.files,0);
  const missing=await mf.dispatchFetch('http://flim/blobs/missing',{method:'POST',headers:{'x-flim-key':h.welcome.blob},body:sha(body)});
  assert.equal(await missing.text(),sha(body));
  assert.equal((await file(h.welcome.blob,body)).status,201);
  assert.equal((await file(h.welcome.blob,body,'GET')).status,200);
  assert.equal((await usage()).uploads.used,2);
});

test("worker startup restores a missing reservation alarm", async t => {
  const {mf,options,create,api,state}=await setup(t);
  const room=await create();await state.testForgetAlarm();
  await mf.setOptions(options);
  assert.equal((await api('/rooms/status',room)).data.state,'waiting');
  const namespace=await mf.getDurableObjectNamespace('ROOM');
  const restored=namespace.get(namespace.idFromName('private-room-01'));
  const alarm=await restored.testAlarmTime();
  assert.ok(alarm>Date.now() && alarm<=Date.parse(room.expiresAt));
});
