// One bounded callback at a time. The warm worker owns no committed gameplay state.
(function(root) {
  'use strict';
  class ObjectiveTransport {
    constructor({makeWorker, now = () => performance.now(),
      timer = (fn, ms) => setTimeout(fn, ms), clear = id => clearTimeout(id)}) {
      Object.assign(this, {makeWorker, now, timer, clear});
      this.serial = 0; this.job = null; this.worker = null;
      this.evalTotalMs = 0; this.evalMaxMs = 0;
      this.completed = 0; this.totalMs = 0; this.maxMs = 0; this.wasmBytes = 0;
    }
    start(bytes) {
      if (!(bytes instanceof Uint8Array) || bytes.length > 131072 || this.job
          || this.serial === 0xffffffff) return 0;
      const id = ++this.serial;
      this.job = {id, state: 0, bytes: null, started: this.now()};
      try {
        if (!this.worker) {
          this.worker = this.makeWorker();
          const worker = this.worker;
          worker.onmessage = ({data}) => {
            if (this.worker !== worker) return;
            if (!this.job || data?.id !== this.job.id) { this.reset(); return; }
            this.finish(data);
          };
          worker.onerror = () => { if (this.worker === worker) this.finish(null); };
        }
        this.job.deadline = this.timer(() => {
          if (this.job?.id === id) this.finish(null);
        }, 5000);
        this.worker.postMessage({mode: 'objective', id, bytes: bytes.slice()});
      } catch (_) { this.finish(null); }
      return id;
    }
    finish(result) {
      const job = this.job;
      if (!job || job.state) return;
      this.clear(job.deadline);
      if (this.now() - job.started >= 5000 || !(result?.bytes instanceof Uint8Array)
          || result.bytes.length < 8 || result.bytes.length > 49152
          || new DataView(result.bytes.buffer, result.bytes.byteOffset).getUint32(0) !== 0x50455231) {
        this.lastError = result?.error || "Invalid or expired objective worker reply";
        job.state = -1;
        if (this.worker) this.worker.terminate();
        this.worker = null;
      } else {
        job.bytes = result.bytes.slice(); job.state = 1;
        const ms = this.now() - job.started;
        ++this.completed; this.totalMs += ms; this.maxMs = Math.max(this.maxMs, ms);
        this.wasmBytes = result.wasmBytes || 0;
        this.evalTotalMs += result.evalMs || 0;
        this.evalMaxMs = Math.max(this.evalMaxMs, result.evalMs || 0);
      }
    }
    status(id) { return this.job?.id === id ? this.job.state : -1; }
    read(id) { return this.job?.id === id ? this.job.bytes : null; }
    close(id) {
      if (this.job?.id !== id) return;
      if (!this.job.state) this.finish(null);
      this.job = null;
    }
    reset() {
      if (this.job) this.close(this.job.id);
      if (this.worker) this.worker.terminate();
      this.worker = null;
    }
  }
  root.BoatPluginObjectiveTransport = ObjectiveTransport;
  if (typeof module === 'object') module.exports = {ObjectiveTransport};
})(globalThis);
