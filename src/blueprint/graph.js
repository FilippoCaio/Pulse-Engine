// ─── Blueprint graph model + serialization ───
import { NODE_TYPES } from './nodes.js';

let NEXT_GRAPH_ID = 1;
let NEXT_NODE_ID = 1;

export class BPNode {
  constructor(type, x = 0, y = 0) {
    this.id = NEXT_NODE_ID++;
    this.type = type;
    this.x = x;
    this.y = y;
    const def = NODE_TYPES[type];
    this.props = {};
    if (def.props) Object.assign(this.props, def.props);
    // literal values for unconnected data inputs
    for (const inp of def.inputs || []) {
      if (inp.kind !== 'exec' && inp.default !== undefined) {
        this.props['in_' + inp.name] = Array.isArray(inp.default) ? [...inp.default] : inp.default;
      }
    }
  }
}

export class Graph {
  constructor(name = 'Nuovo grafo') {
    this.id = NEXT_GRAPH_ID++;
    this.name = name;
    this.nodes = [];
    this.links = []; // { from: [nodeId, pinName], to: [nodeId, pinName] }
  }

  addNode(type, x, y) {
    const n = new BPNode(type, x, y);
    this.nodes.push(n);
    return n;
  }

  removeNode(node) {
    this.nodes = this.nodes.filter(n => n !== node);
    this.links = this.links.filter(l => l.from[0] !== node.id && l.to[0] !== node.id);
  }

  getNode(id) { return this.nodes.find(n => n.id === id) || null; }

  // add link; an input pin accepts only one connection
  connect(fromNode, fromPin, toNode, toPin) {
    this.links = this.links.filter(l => !(l.to[0] === toNode && l.to[1] === toPin));
    this.links.push({ from: [fromNode, fromPin], to: [toNode, toPin] });
  }

  linkInto(nodeId, pinName) {
    return this.links.find(l => l.to[0] === nodeId && l.to[1] === pinName) || null;
  }

  linksFrom(nodeId, pinName) {
    return this.links.filter(l => l.from[0] === nodeId && l.from[1] === pinName);
  }

  serialize() {
    return {
      id: this.id,
      name: this.name,
      nodes: this.nodes.map(n => ({ id: n.id, type: n.type, x: n.x, y: n.y, props: JSON.parse(JSON.stringify(n.props)) })),
      links: this.links.map(l => ({ from: [...l.from], to: [...l.to] })),
    };
  }

  static deserialize(data) {
    const g = new Graph(data.name);
    g.id = data.id;
    NEXT_GRAPH_ID = Math.max(NEXT_GRAPH_ID, data.id + 1);
    g.nodes = (data.nodes || []).filter(n => NODE_TYPES[n.type]).map(nd => {
      const n = new BPNode(nd.type, nd.x, nd.y);
      n.id = nd.id;
      NEXT_NODE_ID = Math.max(NEXT_NODE_ID, nd.id + 1);
      Object.assign(n.props, nd.props || {});
      return n;
    });
    const ids = new Set(g.nodes.map(n => n.id));
    g.links = (data.links || []).filter(l => ids.has(l.from[0]) && ids.has(l.to[0]));
    return g;
  }
}
