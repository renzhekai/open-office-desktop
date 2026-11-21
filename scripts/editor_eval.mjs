// Evaluates JavaScript in the editor frame (frameEditor) of an Open Office instance whose
// profile turned on the engine's debug port (127.0.0.1:8080). Used by verify_office_kinds.py
// to change and read spreadsheets and presentations the way the editor itself does.
// Usage: node scripts/editor_eval.mjs <file.js | -e "code">   (the code may await; its value is printed as JSON)
import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const code = args[0] === "-e" ? args[1] : readFileSync(args[0], "utf8");
const targets = await (await fetch("http://127.0.0.1:8080/json")).json();
const page = targets.find((t) => t.type === "page" && /index\.html/.test(t.url) && t.webSocketDebuggerUrl)
  || targets.find((t) => t.type === "page" && t.webSocketDebuggerUrl);
if (!page) { console.error("no page"); process.exit(2); }
const ws = new WebSocket(page.webSocketDebuggerUrl);
let nextId = 1;
const pending = new Map();
const contexts = [];
ws.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.id && pending.has(message.id)) { pending.get(message.id)(message); pending.delete(message.id); }
  if (message.method === "Runtime.executionContextCreated") contexts.push(message.params.context);
};
const send = (method, params = {}) => new Promise((resolve) => {
  const id = nextId++;
  pending.set(id, resolve);
  ws.send(JSON.stringify({ id, method, params }));
});
await new Promise((resolve) => (ws.onopen = resolve));
const frames = [];
const walk = (node) => { frames.push(node.frame); (node.childFrames || []).forEach(walk); };
walk((await send("Page.getFrameTree")).result.frameTree);
await send("Runtime.enable");
await new Promise((resolve) => setTimeout(resolve, 300));
const editor = frames.find((f) => f.name === "frameEditor");
const context = editor && contexts.find((c) => c.auxData && c.auxData.frameId === editor.id && c.auxData.isDefault);
if (!context) { console.error("no editor frame"); process.exit(3); }
const result = await send("Runtime.evaluate", {
  expression: `(async () => { ${code} })()`, contextId: context.id, awaitPromise: true, returnByValue: true, timeout: 60000,
});
if (result.result.exceptionDetails) {
  console.log(JSON.stringify({ exception: JSON.stringify(result.result.exceptionDetails).slice(0, 2000) }));
  process.exit(4);
}
console.log(JSON.stringify(result.result.result.value));
ws.close();
