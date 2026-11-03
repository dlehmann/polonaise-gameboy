/*
 * play.js - the handheld on the page: runs polonaise.gb in the emulator of gb.js, draws it
 * on the screen, plays the sound and connects the keyboard and the buttons on the device.
 * The cartridge RAM (the high score list) is kept in the browser's localStorage.
 */
'use strict';

(() => {
    const PALETTE = [[0xc4, 0xcf, 0xa1], [0x8b, 0x95, 0x6d], [0x4d, 0x53, 0x3c], [0x1f, 0x1f, 0x1f]];
    const FPS = 4194304 / 70224;
    const SAVE_KEY = 'polonaise-gb.sram';
    const AHEAD = 0.1;      // seconds of sound queued ahead
    const KEYS = {
        ArrowRight: 'right', ArrowLeft: 'left', ArrowUp: 'up', ArrowDown: 'down',
        KeyD: 'right', KeyA: 'left', KeyW: 'up', KeyS: 'down',
        KeyX: 'a', KeyK: 'a', KeyZ: 'b', KeyY: 'b', KeyJ: 'b',
        Enter: 'start', Space: 'start', ShiftLeft: 'select', ShiftRight: 'select', Backspace: 'select',
    };

    const device = document.getElementById('device');
    const canvas = document.getElementById('screen');
    const powerButton = document.getElementById('power');
    const muteButton = document.getElementById('mute');
    const ctx = canvas.getContext('2d');
    const image = ctx.createImageData(160, 144);
    let gain = null;
    let gb = null, audio = null, running = false, muted = false, nextAudio = 0, last = 0, due = 0;

    /* a button is held for at least two frames, so that a short tap reaches the game, which
     * looks at the buttons once per frame */
    let frames = 0;
    const pressedAt = {}, release = new Set();
    function press(b, down) {
        if (!gb)
            return;
        if (down) {
            release.delete(b);
            pressedAt[b] = frames;
            gb.setButton(b, true);
        } else if (frames - (pressedAt[b] ?? -9) < 2) {
            release.add(b);
        } else {
            gb.setButton(b, false);
        }
    }

    function draw() {
        const px = gb.frame, d = image.data;
        for (let i = 0, j = 0; i < px.length; i++, j += 4) {
            const c = PALETTE[px[i]];
            d[j] = c[0]; d[j + 1] = c[1]; d[j + 2] = c[2]; d[j + 3] = 255;
        }
        ctx.putImageData(image, 0, 0);
    }

    /* the sound of the frames run since the last call, queued right after what is queued */
    function queueAudio() {
        const s = gb.takeAudio();
        if (!s.length)
            return;
        const n = s.length >> 1, buf = audio.createBuffer(2, n, audio.sampleRate);
        const l = buf.getChannelData(0), r = buf.getChannelData(1);
        for (let i = 0; i < n; i++) {
            l[i] = s[i * 2];
            r[i] = s[i * 2 + 1];
        }
        const src = audio.createBufferSource();
        src.buffer = buf;
        src.connect(gain);
        src.start(nextAudio);
        nextAudio += n / audio.sampleRate;
    }

    function runFrame() {
        gb.runFrame();
        frames++;
        for (const b of release)
            if (frames - pressedAt[b] >= 2) {
                gb.setButton(b, false);
                release.delete(b);
            }
    }

    /*
     * With sound, the sound card sets the pace: frames are run until about 0.1 s of sound is
     * queued, so the music never has gaps and never drifts (a pace of its own for the picture
     * would leave gaps whenever the browser is a little late, and the music would drag).
     * Without sound, the clock of the animation frames does.
     */
    function loop(t) {
        if (!running)
            return;
        let n = 0;
        if (audio && audio.state === 'running') {
            const now = audio.currentTime;
            if (nextAudio < now + 0.01)
                nextAudio = now + 0.05;     // after a pause, or when the page was too slow
            while (nextAudio < now + AHEAD && n < 8) {
                runFrame();
                queueAudio();
                n++;
            }
            last = 0;
        } else {
            due += (last ? Math.min(t - last, 100) : 1000 / 60) * FPS / 1000;
            last = t;
            while (due >= 1 && n < 4) {
                runFrame();
                gb.takeAudio();
                due -= 1;
                n++;
            }
            if (due > 4)
                due = 0;
        }
        if (n)
            draw();
        requestAnimationFrame(loop);
    }

    function loadSram() {
        try {
            const s = localStorage.getItem(SAVE_KEY);
            if (s) {
                const bytes = Uint8Array.from(atob(s), c => c.charCodeAt(0));
                gb.sram.set(bytes.subarray(0, gb.sram.length));
            }
        } catch (e) {
            // no storage: the list lasts until the page is closed
        }
    }

    function saveSram(sram) {
        try {
            let s = '';
            for (const b of sram)
                s += String.fromCharCode(b);
            localStorage.setItem(SAVE_KEY, btoa(s));
        } catch (e) {
            // see above
        }
    }

    function start() {
        running = true;
        last = 0;
        due = 0;
        device.classList.add('on');
        requestAnimationFrame(loop);
    }

    function stop() {
        running = false;
        device.classList.remove('on');
    }

    async function power() {
        if (gb) {
            if (running) stop(); else start();
            return;
        }
        powerButton.disabled = true;
        try {
            audio = new AudioContext();
            gain = audio.createGain();
            gain.connect(audio.destination);
        } catch (e) {
            audio = null;
        }
        try {
            const res = await fetch('polonaise.gb');
            if (!res.ok)
                throw new Error(res.status);
            gb = new GameBoy(new Uint8Array(await res.arrayBuffer()), { sampleRate: audio ? audio.sampleRate : 48000 });
        } catch (e) {
            device.classList.add('failed');
            powerButton.disabled = false;
            return;
        }
        loadSram();
        gb.onSave = saveSram;
        powerButton.disabled = false;
        device.classList.add('booted');
        start();
        device.focus({ preventScroll: true });
    }

    powerButton.addEventListener('click', power);
    document.getElementById('switch').addEventListener('click', () => {
        if (!gb) power(); else if (running) stop(); else start();
    });
    muteButton.addEventListener('click', () => {
        muted = !muted;
        if (gain)
            gain.gain.value = muted ? 0 : 1;
        muteButton.setAttribute('aria-pressed', String(muted));
        muteButton.classList.toggle('muted', muted);
    });

    /* the keyboard works while the device has the focus, or while the game runs, nothing else
     * has the focus and the device can be seen */
    function forGame() {
        const el = document.activeElement;
        if (el === device)
            return true;
        if (!running || (el && el !== document.body))
            return false;
        const r = device.getBoundingClientRect();
        return r.bottom > 0 && r.top < innerHeight;
    }

    function key(ev, down) {
        const b = KEYS[ev.code];
        if (!b || !gb || ev.altKey || ev.ctrlKey || ev.metaKey || (down && !forGame()))
            return;
        ev.preventDefault();
        if (ev.repeat)
            return;
        press(b, down);
        const el = device.querySelector(`[data-btn="${b}"]`);
        if (el)
            el.classList.toggle('down', down);
        else
            pad.dataset.dir = down ? b : '';
    }
    document.addEventListener('keydown', ev => key(ev, true));
    document.addEventListener('keyup', ev => key(ev, false));
    device.addEventListener('blur', () => {
        if (gb)
            for (const b of ['right', 'left', 'up', 'down', 'a', 'b', 'select', 'start'])
                press(b, false);
        device.querySelectorAll('.down').forEach(el => el.classList.remove('down'));
    });

    /* the buttons on the device, by mouse and touch */
    for (const el of device.querySelectorAll('[data-btn]')) {
        const b = el.dataset.btn;
        const up = () => {
            el.classList.remove('down');
            press(b, false);
        };
        el.addEventListener('pointerdown', ev => {
            ev.preventDefault();
            if (!gb) {
                power();
                return;
            }
            el.setPointerCapture(ev.pointerId);
            el.classList.add('down');
            press(b, true);
            device.focus({ preventScroll: true });
        });
        el.addEventListener('pointerup', up);
        el.addEventListener('pointercancel', up);
    }

    /* the cross: the direction from the middle, also while the finger slides */
    const pad = document.getElementById('pad');
    let padDir = null;
    function padAt(ev) {
        const r = pad.getBoundingClientRect();
        const x = ev.clientX - r.left - r.width / 2, y = ev.clientY - r.top - r.height / 2;
        const dir = Math.hypot(x, y) < r.width * 0.1 ? null :
            Math.abs(x) > Math.abs(y) ? (x > 0 ? 'right' : 'left') : (y > 0 ? 'down' : 'up');
        if (dir === padDir)
            return;
        if (padDir)
            press(padDir, false);
        if (dir)
            press(dir, true);
        padDir = dir;
        pad.dataset.dir = dir || '';
    }
    pad.addEventListener('pointerdown', ev => {
        ev.preventDefault();
        if (!gb) {
            power();
            return;
        }
        pad.setPointerCapture(ev.pointerId);
        device.focus({ preventScroll: true });
        padAt(ev);
    });
    pad.addEventListener('pointermove', ev => {
        if (pad.hasPointerCapture(ev.pointerId))
            padAt(ev);
    });
    const padUp = () => {
        if (padDir)
            press(padDir, false);
        padDir = null;
        pad.dataset.dir = '';
    };
    pad.addEventListener('pointerup', padUp);
    pad.addEventListener('pointercancel', padUp);

    /* a hidden page pauses the game */
    document.addEventListener('visibilitychange', () => {
        if (!gb)
            return;
        if (document.hidden) {
            device.dataset.wasRunning = running ? '1' : '';
            stop();
        } else if (device.dataset.wasRunning) {
            start();
        }
    });
})();
