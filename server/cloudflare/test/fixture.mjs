import worker, { FlimRoom } from "../dist/worker.mjs";
export default worker;

// RPC hooks exist only in the local test worker, never in wrangler.jsonc.
export class TestRoom extends FlimRoom {
  async testSavedPortableMode() { return (await this.ctx.storage.get('rooms')).map(([code,room])=>({code,portable:room.portableSync})); }
  async testSavedNoteMode() { return (await this.ctx.storage.get('rooms')).map(([code, room]) => ({code, crdt: room.notesCrdt})); }
  async testBudget(kind, n, window = "day", at = Date.now()) {
    this.ctx.storage.sql.exec("INSERT OR REPLACE INTO budget(day,kind,n) VALUES(?,?,?)", new Date(at).toISOString().slice(0, window === "minute" ? 16 : 10), kind, n);
  }
  async testR2Budget(kind, n, month = new Date().toISOString().slice(0, 7)) {
    this.ctx.storage.sql.exec("INSERT OR REPLACE INTO r2_usage(month,kind,n) VALUES(?,?,?)", month, kind, n);
  }
  async testAgeUploads() {
    this.ctx.storage.sql.exec("UPDATE files SET created=? WHERE ready=0", Date.now() - 700000);
    await this.alarm();
  }
  async testLoseReservation() { this.ctx.storage.sql.exec("DELETE FROM files WHERE ready=0"); }
  async testBadLength(key, h, body, length) {
    const url = new URL("http://flim/blob/" + h);
    const response = await this.files(new Request(url, { method: "PUT", headers: { "x-flim-key": key, "content-length": String(length) }, body }), url);
    return response.status;
  }
  async testStorageFull() {
    this.ctx.storage.sql.exec("INSERT INTO files(h,bytes,prefix,ready,created) VALUES(?,?,?,1,?)", "0".repeat(64), 512 * 1024 * 1024, this.rooms.get("01").generation, Date.now());
  }
  async testExpireLocks() {
    for (const ws of this.ctx.getWebSockets()) {
      const a = ws.deserializeAttachment(); a.locks.forEach(l => l.at = Date.now() - 31000); ws.serializeAttachment(a);
    }
    await this.alarm();
  }
  async testAgeRoom() {
    // Authentication deadlines apply only before admission; an old deadline must not end a joined session.
    for (const ws of this.ctx.getWebSockets()) { const a = ws.deserializeAttachment(); a.deadline = Date.now() - 24 * 3600000; ws.serializeAttachment(a); }
    await this.alarm();
  }
  async testPending() { return this.ctx.storage.sql.exec("SELECT * FROM files WHERE ready=0").toArray().length; }
  async testExpireReservations() { this.ctx.storage.sql.exec("UPDATE reservations SET expires=0"); await this.alarm(); }
  async testEndRoom(code = "01") { await this.end(this.rooms.get(code)); }
  async testReservationToken(code) { return this.ctx.storage.sql.exec("SELECT token FROM reservations WHERE code=?", code).toArray()[0]?.token; }
  async testForgetAlarm() { await this.ctx.storage.deleteAlarm(); this.nextAlarm = null; }
  async testAlarmTime() { return this.ctx.storage.getAlarm(); }
  async testBlockMessages() {
    await this.tail;
    this.tail = new Promise(resolve => { this.resumeMessages = resolve; });
  }
  async testResumeMessages() { this.resumeMessages?.(); }
  async testMessageQueue() { return { bytes: this.queuedBytes, count: this.queuedCount, peers: this.queuedMessages.size }; }
  async testCleanupBehindUploads() {
    const prefix = "retired-room";
    for (let i = 0; i < 65; i++) {
      const h = i.toString(16).padStart(64, "0");
      this.ctx.storage.sql.exec("INSERT INTO files(h,bytes,prefix,ready,created) VALUES(?,1,?,0,?)", h, prefix, Date.now() - 700000);
      if (i < 64) this.uploads.set(prefix + "/" + h, new AbortController());
      else await this.env.FILES.put(prefix + "/" + h, "x");
    }
    this.refreshCleanup();
    await this.alarm();
    return this.ctx.storage.sql.exec("SELECT count(*) AS n FROM files WHERE prefix=?", prefix).toArray()[0].n;
  }
}
