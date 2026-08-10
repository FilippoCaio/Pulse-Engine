// ─── Blueprint runtime: interprets a Graph for one entity during play ───
import { NODE_TYPES } from './nodes.js';

const MAX_EXEC_STEPS = 500;
const MAX_EVAL_DEPTH = 64;

export class ScriptInstance {
  constructor(graph, entity, ctx) {
    this.graph = graph;
    this.entity = entity;
    this.ctx = ctx;           // { scene, world, editor, input, dt, time, entity }
    this.eventData = new Map(); // nodeId → outputs provided by the event
    this.dead = false;
  }

  fireEvent(type, data = {}) {
    if (this.dead) return;
    for (const node of this.graph.nodes) {
      if (node.type !== type) continue;
      this.eventData.set(node.id, data);
      // event.key filter
      if (type === 'event.key' && data.key !== node.props.key) continue;
      this.execFrom(node, 'exec');
    }
  }

  execFrom(eventNode, pinName) {
    let steps = 0;
    let current = this.followExec(eventNode.id, pinName);
    while (current && steps++ < MAX_EXEC_STEPS) {
      const def = NODE_TYPES[current.type];
      if (!def || !def.exec) break;
      let nextPin;
      try {
        nextPin = def.exec(this.ctx, current, (name) => this.evalInput(current, name, 0));
      } catch (err) {
        this.ctx.editor.log(`Errore blueprint "${this.graph.name}" nel nodo ${def.title}: ${err.message}`, 'error');
        break;
      }
      if (this.dead) break;
      if (!nextPin) break;
      current = this.followExec(current.id, nextPin);
    }
  }

  followExec(nodeId, pinName) {
    const link = this.graph.linksFrom(nodeId, pinName)[0];
    if (!link) return null;
    return this.graph.getNode(link.to[0]);
  }

  evalInput(node, pinName, depth) {
    if (depth > MAX_EVAL_DEPTH) return 0;
    const link = this.graph.linkInto(node.id, pinName);
    if (link) {
      const src = this.graph.getNode(link.from[0]);
      const srcDef = src && NODE_TYPES[src.type];
      if (!srcDef) return 0;
      if (srcDef.isEvent) {
        const data = this.eventData.get(src.id) || {};
        return data[link.from[1]];
      }
      if (srcDef.eval) {
        try {
          const outs = srcDef.eval(this.ctx, src, (name) => this.evalInput(src, name, depth + 1));
          return outs[link.from[1]];
        } catch (err) {
          this.ctx.editor.log(`Errore blueprint nel nodo ${srcDef.title}: ${err.message}`, 'error');
          return 0;
        }
      }
      return 0;
    }
    // no connection → literal stored on the node
    const lit = node.props['in_' + pinName];
    if (lit !== undefined) {
      if (Array.isArray(lit)) return { x: +lit[0] || 0, y: +lit[1] || 0, z: +lit[2] || 0 };
      return lit;
    }
    const def = NODE_TYPES[node.type];
    const inp = (def.inputs || []).find(i => i.name === pinName);
    if (inp && inp.kind === 'entity') return null; // defaults to self in actions
    return inp?.default ?? 0;
  }
}
