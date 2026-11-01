// Sound in the browser (docs/browser/README.md, D2). The WebAssembly kit has no Qt Multimedia,
// so the browser's own Web Audio plays the game's synthesizer (audioengine.h): a script node
// asks for the next samples every ~46 ms and they are made here, as on the desktop. Browsers
// only let sound start after the player touches the page, so the first key press or click
// resumes it.
#ifdef __EMSCRIPTEN__
#include "audioengine.h"

#include <emscripten.h>
#include <cstdint>

static AudioSynthStream *g_stream = nullptr;
static float g_out[8192];                            // (the samples for the browser, -1..1)
static int16_t g_pcm[8192];

extern "C" EMSCRIPTEN_KEEPALIVE void tpp_audio_fill(int n)
{
    if (n > 8192) n = 8192;
    if (!g_stream) { for (int i = 0; i < n; ++i) g_out[i] = 0.0f; return; }
    g_stream->readData(reinterpret_cast<char *>(g_pcm), qint64(n) * qint64(sizeof(int16_t)));
    for (int i = 0; i < n; ++i) g_out[i] = float(g_pcm[i]) / 32768.0f;
}

EM_JS(void, tpp_audio_js_start, (float *out), {
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    if (new URLSearchParams(location.search).has("mute")) return;   // (index.html?mute: no sound at all)
    var ctx;
    try { ctx = new AC({ sampleRate: 44100 }); } catch (e) { ctx = new AC(); }
    var node = ctx.createScriptProcessor(2048, 0, 1);
    node.onaudioprocess = function (e) {
        var data = e.outputBuffer.getChannelData(0);
        _tpp_audio_fill(data.length);
        data.set(HEAPF32.subarray(out >> 2, (out >> 2) + data.length));
    };
    node.connect(ctx.destination);
    var resume = function () { if (ctx.state !== 'running') ctx.resume(); };
    ['keydown', 'mousedown', 'touchstart'].forEach(function (ev) { window.addEventListener(ev, resume, true); });
    // a hidden tab is silent; it plays again when you come back
    document.addEventListener('visibilitychange', function () {
        if (document.hidden) ctx.suspend(); else ctx.resume();
    });
});

void webAudioStart(AudioSynthStream *s)
{
    g_stream = s;
    tpp_audio_js_start(g_out);
}
#endif
