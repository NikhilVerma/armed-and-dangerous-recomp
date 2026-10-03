// Frida agent for tools/record.py: per-function call recording with self-removing hooks.
//
// The hot path is C (CModule), integer-only: the game unmasks x87 exceptions on its threads, so JavaScript
// (all numbers are doubles) must never run there. C callbacks append fixed-size records to a ring buffer;
// JavaScript on Frida's own thread drains it, sends batches, and detaches functions that reached their quota.
//
// Record layout (u32 x 20): [0] seq+1 marker, [1] kind (1 entry, 2 return), [2] function index, [3] tick ms,
//   [4] thread id, entry: [5] caller, [6..13] eax ecx edx ebx esi edi ebp esp, [14..17] stack args 1-4
//   return: [5] eax, [6] edx
'use strict';

const REC_WORDS = 20;
const REC_BYTES = REC_WORDS * 4;
const CAPACITY = 1 << 18;  // records in the ring (20 MB)

const cSource = `
#include <gum/guminterceptor.h>

extern unsigned int GetTickCount(void);

#define REC_WORDS ${REC_WORDS}
#define CAPACITY ${CAPACITY}

extern volatile int writeIndex;
extern unsigned int ring[];
extern volatile int counts[];
extern int quota;

typedef struct { int recorded; } State;

static unsigned int *slot(void)
{
  int i = g_atomic_int_add(&writeIndex, 1);
  return ring + (unsigned int)(i % CAPACITY) * REC_WORDS;
}

void onEnter(GumInvocationContext *ic)
{
  int fn = GPOINTER_TO_INT(gum_invocation_context_get_listener_function_data(ic));
  State *st = GUM_IC_GET_INVOCATION_DATA(ic, State);
  int n = g_atomic_int_add(&counts[fn], 1);
  st->recorded = n < quota;
  if (!st->recorded) return;
  GumCpuContext *c = ic->cpu_context;
  unsigned int *sp = (unsigned int *)c->esp;
  unsigned int *r = slot();
  r[1] = 1; r[2] = fn; r[3] = GetTickCount(); r[4] = gum_invocation_context_get_thread_id(ic);
  r[5] = sp[0];
  r[6] = c->eax; r[7] = c->ecx; r[8] = c->edx; r[9] = c->ebx;
  r[10] = c->esi; r[11] = c->edi; r[12] = c->ebp; r[13] = c->esp;
  r[14] = sp[1]; r[15] = sp[2]; r[16] = sp[3]; r[17] = sp[4];
  *(volatile unsigned int *)&r[0] = 1;  /* x86 stores are ordered: marker last */
}

void onLeave(GumInvocationContext *ic)
{
  State *st = GUM_IC_GET_INVOCATION_DATA(ic, State);
  if (!st->recorded) return;
  GumCpuContext *c = ic->cpu_context;
  unsigned int *r = slot();
  r[1] = 2; r[2] = GPOINTER_TO_INT(gum_invocation_context_get_listener_function_data(ic));
  r[3] = GetTickCount(); r[4] = gum_invocation_context_get_thread_id(ic);
  r[5] = c->eax; r[6] = c->edx;
  *(volatile unsigned int *)&r[0] = 1;  /* x86 stores are ordered: marker last */
}
`;

let cm = null;
let ring, counts, writeIndexPtr, quotaPtr;
let readIndex = 0;
let functions = [];
let quota = 50;
const listeners = [];
const detached = new Set();

function drain() {
  const w = writeIndexPtr.readS32();
  const out = [];
  while (readIndex < w && out.length < 50000) {
    const base = ring.add((readIndex % CAPACITY) * REC_BYTES);
    if (base.readU32() === 0) break;  // still being written
    const rec = [];
    for (let k = 1; k < REC_WORDS - 2; k++) rec.push(base.add(k * 4).readU32());
    rec[1] = functions[rec[1]];  // index -> address
    out.push(rec[0] === 2 ? rec.slice(0, 6) : rec);
    base.writeU32(0);
    readIndex++;
  }
  if (out.length) send({ records: out });
  // detach functions that reached their quota (the C side already stopped recording them)
  for (let i = 0; i < functions.length; i++) {
    if (!detached.has(i) && counts.add(i * 4).readS32() >= quota) {
      detached.add(i);
      listeners[i].detach();
    }
  }
}

recv('config', function (msg) {
  functions = msg.functions.map(f => f[0]);
  quota = msg.callsPerFunction;
  ring = Memory.alloc(CAPACITY * REC_BYTES);
  counts = Memory.alloc(functions.length * 4);
  writeIndexPtr = Memory.alloc(4);
  quotaPtr = Memory.alloc(4);
  quotaPtr.writeS32(quota);
  cm = new CModule(cSource, {
    GetTickCount: Process.getModuleByName('kernel32.dll').getExportByName('GetTickCount'),
    writeIndex: writeIndexPtr, ring: ring, counts: counts, quota: quotaPtr,
  });
  let ok = 0;
  const failed = [];
  for (let i = 0; i < functions.length; i++) {
    try {
      listeners[i] = Interceptor.attach(ptr(functions[i]), { onEnter: cm.onEnter }, ptr(i));  // entry only: return hooks broke x87 returns
      ok++;
    } catch (e) {
      listeners[i] = { detach() {} };
      detached.add(i);
      failed.push([functions[i], String(e)]);
    }
  }
  Interceptor.flush();
  setInterval(drain, 100);
  send({ hooked: ok, failed: failed.length, failures: failed.slice(0, 50) });
});
