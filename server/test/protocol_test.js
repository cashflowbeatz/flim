const assert = require("node:assert/strict");
const test = require("node:test");
const { validOp } = require("../dist/protocol");

test("normal project, preset, note and parameter payloads retain their wire format", () => {
  const operations = [
    { kind: "project.load", name: 'A "song".flp', data: "QUJD", samples: [{ p: "YQA=", h: "a".repeat(64), s: 4 }] },
    { kind: "plug.full", nch: 1, items: [{ unit: "ch", ch: 0, data: "QUJD" }] },
    { kind: "notes.full", items: [{ pat: 1, notes: [[0, 60, 96, 100]] }] },
    { kind: "state.full", vals: [[1, 2], [3, 4]] },
    { kind: "future.extension", values: { custom: [1, { label: '{"kind":"project.load"}' }] } },
  ];
  for (const op of operations) {
    const encoded = JSON.stringify(op);
    assert.equal(validOp(JSON.parse(encoded)), true, op.kind);
    assert.equal(JSON.stringify(op), encoded);
  }
});

test("control fields cannot be smuggled through objects, arrays or escaped property names", () => {
  const disguised = { kind: "param.set", values: [{ 'prefix"kind': "project.load" }] };
  assert.ok(JSON.stringify(disguised).includes('"kind":"project.load"'), "the native substring parser would see a project load");
  const invalid = [
    disguised, { kind: "param.set", t: "welcome" },
    { kind: "param.set", values: [{ kind: "project.load" }] },
    { kind: "param.set", values: [{ t: "leader_left" }] },
    { kind: "param.set", values: { 'prefix\\t': "welcome" } },
    { kind: "param.set", values: { 'prefix\u0000t': "welcome" } },
    { kind: "param.set", key: {} }, { kind: "param.set", key: "" },
    { kind: "x".repeat(65) }, { kind: "" }, null, [], 1,
  ];
  for (const op of invalid) assert.equal(validOp(op), false, JSON.stringify(op));
});
