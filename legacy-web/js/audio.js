// AudioContext, busses and a soft limiter. Channels 0–1 = keys, 2–5 = style
// parts, 9–10 = drums/percussion.

export function createAudio() {
  const AC = window.AudioContext || window.webkitAudioContext;
  const ctx = new AC({ latencyHint: 'interactive' }); // native sample rate: a forced rate adds a resampler and latency on Android
  const master = ctx.createGain();
  master.gain.value = 0.9;
  const limiter = ctx.createDynamicsCompressor();
  limiter.threshold.value = -6;
  limiter.knee.value = 4;
  limiter.ratio.value = 12;
  limiter.attack.value = 0.002;
  limiter.release.value = 0.12;
  master.connect(limiter).connect(ctx.destination);

  const bus = {};
  for (const name of ['keys', 'style', 'drums']) {
    bus[name] = ctx.createGain();
    bus[name].gain.value = 0.8;
    bus[name].connect(master);
  }
  const chan = {};
  for (let ch = 0; ch < 16; ch++) {
    chan[ch] = ctx.createGain();
    chan[ch].gain.value = 1;
    chan[ch].connect(ch <= 1 ? bus.keys : ch === 9 || ch === 10 ? bus.drums : bus.style);
  }

  return {
    ctx, master, bus, chan,
    routeFor: (ch) => chan[ch] || chan[0],
    resume: () => (ctx.state !== 'running' ? ctx.resume() : Promise.resolve()),
    latencyMs: () => Math.round(((ctx.baseLatency || 0) + (ctx.outputLatency || 0)) * 1000),
    setBus: (name, v) => { bus[name].gain.setTargetAtTime(v, ctx.currentTime, 0.02); },
    setMaster: (v) => { master.gain.setTargetAtTime(v, ctx.currentTime, 0.02); },
  };
}
