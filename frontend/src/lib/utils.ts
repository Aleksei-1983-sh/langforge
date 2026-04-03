export function escapeHtml(s: string): string {
  return s.replace(/[&<>"']/g, c =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' } as Record<string, string>)[c]
  );
}

export function $<T extends Element = Element>(sel: string, root: ParentNode = document): T | null {
  return root.querySelector<T>(sel);
}

export function mustFind<T extends Element = Element>(sel: string, root: ParentNode = document): T {
  const el = root.querySelector<T>(sel);
  if (!el) throw new Error(`Missing DOM element: ${sel}`);
  return el;
}
