import assert from "node:assert/strict";

import { consumeSseBuffer, extractStreamText } from "../src/lib/streamProtocol.js";

const canonicalSample =
  'event: token\r\ndata: {"text":"Решение: ","token":"Решение: ","conversation_id":"c1"}\r\n\r\n' +
  'event: token\r\ndata: {"text":"x = 2","token":"x = 2","conversation_id":"c1"}\r\n\r\n' +
  'event: done\r\ndata: {"conversation_id":"c1","method":"math_linear","runtime_query_kind":"math"}\r\n\r\n';

const canonicalParsed = consumeSseBuffer(canonicalSample);
assert.equal(canonicalParsed.remainder, "");
assert.equal(canonicalParsed.events.length, 3);
assert.equal(canonicalParsed.events[0].eventType, "token");
assert.equal(extractStreamText(canonicalParsed.events[0].payload), "Решение: ");
assert.equal(extractStreamText(canonicalParsed.events[1].payload), "x = 2");
assert.equal(canonicalParsed.events[2].eventType, "done");
assert.equal(canonicalParsed.events[2].payload.method, "math_linear");

const legacySample =
  'event: message\r\ndata: {"token":"Разбор предыдущего решения по шагам.","conversation_id":"c2"}\r\n\r\n';
const legacyParsed = consumeSseBuffer(legacySample);
assert.equal(legacyParsed.events.length, 1);
assert.equal(legacyParsed.events[0].eventType, "message");
assert.equal(extractStreamText(legacyParsed.events[0].payload), "Разбор предыдущего решения по шагам.");

const partialSample =
  'event: token\r\ndata: {"text":"alpha","token":"alpha"}\r\n\r\n' +
  'event: token\r\ndata: {"text":"beta"';
const partialParsed = consumeSseBuffer(partialSample);
assert.equal(partialParsed.events.length, 1);
assert.equal(extractStreamText(partialParsed.events[0].payload), "alpha");
assert.match(partialParsed.remainder, /beta/);

console.log("frontend smoke contracts: ok");
