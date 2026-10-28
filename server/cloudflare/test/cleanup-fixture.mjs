import worker, { FlimRoom } from "../dist/worker.mjs";
export default worker;

// Fault injection is available only through this local test entry point.
export class CleanupTestRoom extends FlimRoom {
  constructor(ctx, env) {
    const faults = { putAfterCommit: false, deletes: 0 };
    super(ctx, {
      ...env,
      FILES: {
        get: (...args) => env.FILES.get(...args),
        async put(...args) {
          const object = await env.FILES.put(...args);
          if (faults.putAfterCommit) {
            faults.putAfterCommit = false;
            throw new Error("Injected failure after R2 committed the upload");
          }
          return object;
        },
        async delete(...args) {
          if (faults.deletes > 0) {
            faults.deletes--;
            throw new Error("Injected R2 cleanup failure");
          }
          return env.FILES.delete(...args);
        }
      }
    });
    this.faults = faults;
  }
  async testFailUploadAndDeletes() {
    this.faults.putAfterCommit = true;
    this.faults.deletes = 2;
  }
  async testCleanup() {
    this.ctx.storage.sql.exec("UPDATE files SET created=? WHERE ready=0", Date.now() - 700000);
    try { await this.alarm(); return null; }
    catch (error) { return error.message; }
  }
  async testAlarmTime() { return this.ctx.storage.getAlarm(); }
}
