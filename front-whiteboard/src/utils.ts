import equal from "fast-deep-equal";

export function joinPath(paths: string[]): string {
  let path = paths[0]
  for (let i of paths.slice(1)) {
    path += path.endsWith('/') || i.startsWith('/') ? i : `/${i}`;
  }
  return path;
}

export function objectDiff(from: any, to: any): any {
  if (typeof(from) != 'object' || typeof(to) != 'object') {
    return null;
  }

  const diff: any = {};
  
  for (let i of Object.keys(from)) {
    if (!to.hasOwnProperty(i)) {
      diff[i] = null;
    }
  }

  for (let i of Object.keys(to)) {
    if (!equal(from[i], to[i])) {
      diff[i] = to[i];
    }
  }

  return diff;
}