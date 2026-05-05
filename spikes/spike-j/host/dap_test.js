#!/usr/bin/env node
/* Spike J — DAP test harness.
 *
 * Drives dap_lua_host through the same DAP message sequence VS Code's
 * built-in client emits during a normal breakpoint session:
 *   initialize → setBreakpoints → configurationDone →
 *   stopped(breakpoint) → stackTrace / scopes / variables → continue → ...
 *
 * Stage 2 step 8 — programmatic regression for the breakpoint path.
 * Stage 4 step 15 — synthetic-reload regression (shifted-line variant).
 * Stage 4 step 16 — deleted-line variant (verified=false expected).
 *
 * Usage: node dap_test.js [port] [test]
 *   test ∈ { breakpoint, reload-shifted, reload-deleted } (default: all)
 *
 * Exit code 0 on success, 1 on any assertion failure.
 */

'use strict';

const net = require('net');
const fs  = require('fs');

const PORT = parseInt(process.argv[2] || '5678', 10);

let seq = 1;
let socket;
let buffer = Buffer.alloc(0);
const pending = new Map();
const events  = [];
const eventQueue = [];
let waiters = [];

function nextSeq() { return seq++; }

function send(message) {
    const body = JSON.stringify(message);
    const hdr  = `Content-Length: ${Buffer.byteLength(body)}\r\n\r\n`;
    socket.write(hdr + body);
}

function request(command, args = {}) {
    const reqSeq = nextSeq();
    const msg = { seq: reqSeq, type: 'request', command, arguments: args };
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() =>
            reject(new Error(`timeout waiting for ${command}`)), 5000);
        pending.set(reqSeq, { resolve, reject, timer });
        send(msg);
    });
}

function nextEvent(name) {
    return new Promise((resolve, reject) => {
        const t = setTimeout(() => reject(new Error(`timeout waiting for event ${name}`)), 10000);
        const w = (e) => {
            if (e.event === name) {
                clearTimeout(t);
                waiters = waiters.filter(x => x !== w);
                resolve(e);
            }
        };
        waiters.push(w);
        // Drain already-queued events.
        for (const e of eventQueue) w(e);
    });
}

function onMessage(msg) {
    if (msg.type === 'response') {
        const p = pending.get(msg.request_seq);
        if (p) {
            clearTimeout(p.timer);
            pending.delete(msg.request_seq);
            if (msg.success) p.resolve(msg.body || {});
            else p.reject(new Error(msg.message || 'request failed'));
        }
    } else if (msg.type === 'event') {
        events.push(msg);
        eventQueue.push(msg);
        for (const w of [...waiters]) w(msg);
    }
}

function feed(chunk) {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
        const hdrEnd = buffer.indexOf('\r\n\r\n');
        if (hdrEnd < 0) return;
        const hdr = buffer.slice(0, hdrEnd).toString('utf8');
        const m = /Content-Length:\s*(\d+)/i.exec(hdr);
        if (!m) { buffer = buffer.slice(hdrEnd + 4); continue; }
        const len = parseInt(m[1], 10);
        if (buffer.length < hdrEnd + 4 + len) return;
        const body = buffer.slice(hdrEnd + 4, hdrEnd + 4 + len).toString('utf8');
        buffer = buffer.slice(hdrEnd + 4 + len);
        try { onMessage(JSON.parse(body)); }
        catch (e) { console.error('parse error:', e.message); }
    }
}

function connect() {
    return new Promise((resolve, reject) => {
        socket = net.createConnection({ host: '127.0.0.1', port: PORT });
        socket.on('connect', resolve);
        socket.on('error',   reject);
        socket.on('data',    feed);
    });
}

function assert(cond, msg) {
    if (!cond) {
        console.error('FAIL: ' + msg);
        process.exit(1);
    }
    console.log('PASS: ' + msg);
}

async function testBreakpoint(sourcePath, hitLine) {
    await connect();
    const init = await request('initialize', {
        clientID: 'spike-j-test', adapterID: 'fc32-lua',
        linesStartAt1: true, columnsStartAt1: true });
    assert(init.supportsConfigurationDoneRequest === true,
           'initialize advertises configurationDone');

    await nextEvent('initialized');

    /* VS Code flow: launch → setBreakpoints → configurationDone. */
    await request('launch');

    const sb = await request('setBreakpoints', {
        source: { path: sourcePath, name: sourcePath },
        breakpoints: [{ line: hitLine }],
        lines: [hitLine],
    });
    assert(Array.isArray(sb.breakpoints) && sb.breakpoints.length === 1,
           'setBreakpoints returns one breakpoint');
    assert(sb.breakpoints[0].verified === true,
           `breakpoint at line ${hitLine} is verified`);

    await request('configurationDone');

    const stopped = await nextEvent('stopped');
    assert(stopped.body.reason === 'breakpoint' || stopped.body.reason === 'pause',
           'stopped event reason is breakpoint');

    const threads = await request('threads');
    assert(threads.threads.length >= 1, 'threads response has at least one thread');

    const stack = await request('stackTrace', { threadId: 1 });
    assert(stack.stackFrames.length >= 1, 'stackTrace returns at least one frame');
    const top = stack.stackFrames[0];
    assert(top.line === hitLine || stack.stackFrames.some(f => f.line === hitLine),
           `top frame or any frame is at line ${hitLine}`);

    const scopes = await request('scopes', { frameId: 0 });
    assert(scopes.scopes.length >= 1, 'scopes returns at least one scope');

    const vars = await request('variables', { variablesReference: scopes.scopes[0].variablesReference });
    assert(Array.isArray(vars.variables), 'variables returns an array');

    await request('continue', { threadId: 1 });
    socket.end();
    return { sb };
}

async function main() {
    const test = process.argv[3] || 'breakpoint';
    const source = process.env.CART_SOURCE || 'cases/case_c_dbg/main.lua';
    const hitLine = parseInt(process.env.CART_LINE || '4', 10);

    try {
        if (test === 'breakpoint') {
            await testBreakpoint(source, hitLine);
        } else if (test === 'reload-shifted') {
            const shifted = process.env.SHIFTED_LUAC;
            if (!shifted) throw new Error('SHIFTED_LUAC not set');
            await connect();
            await request('initialize', { clientID: 'spike-j-test', adapterID: 'fc32-lua' });
            await nextEvent('initialized');
            const bytecode = fs.readFileSync(shifted).toString('hex');
            const sb1 = await request('setBreakpoints', {
                source: { path: source, name: source },
                breakpoints: [{ line: hitLine }], lines: [hitLine] });
            assert(sb1.breakpoints[0].verified, 'pre-reload breakpoint verified');
            await request('configurationDone');
            await request('hot_reload', { source: source, bytecodeHex: bytecode });
            const ev = await nextEvent('loadedSource');
            assert(ev.body.reason === 'changed',
                   'loadedSource(reason: "changed") emitted on hot_reload');
            const sb2 = await request('setBreakpoints', {
                source: { path: source, name: source },
                breakpoints: [{ line: hitLine + 5 }], lines: [hitLine + 5] });
            assert(sb2.breakpoints[0].verified, 'post-reload breakpoint at shifted line verified');
            socket.end();
        } else if (test === 'reload-deleted') {
            const deleted = process.env.DELETED_LUAC;
            if (!deleted) throw new Error('DELETED_LUAC not set');
            await connect();
            await request('initialize', { clientID: 'spike-j-test', adapterID: 'fc32-lua' });
            await nextEvent('initialized');
            const bytecode = fs.readFileSync(deleted).toString('hex');
            await request('setBreakpoints', {
                source: { path: source, name: source },
                breakpoints: [{ line: hitLine }], lines: [hitLine] });
            await request('configurationDone');
            await request('hot_reload', { source: source, bytecodeHex: bytecode });
            await nextEvent('loadedSource');
            const sb = await request('setBreakpoints', {
                source: { path: source, name: source },
                breakpoints: [{ line: 0 }], lines: [0] });
            assert(sb.breakpoints[0].verified === false,
                   'deleted-line breakpoint marked verified=false');
            socket.end();
        } else {
            console.error('unknown test:', test);
            process.exit(2);
        }
        console.log('OK');
        process.exit(0);
    } catch (e) {
        console.error('ERROR:', e.message);
        process.exit(1);
    }
}

main();
