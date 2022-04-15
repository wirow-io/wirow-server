export interface SvgPath {
  path: string;
  color?: string;
}

export interface Svg {
  viewbox: number;
  paths: SvgPath[];
}

export type IconSpec = string | Svg;

export function createIconSvg(input: IconSpec): Svg {
  const ret = { viewbox: 32, paths: [] } as Svg;
  const raw = input;
  if (typeof raw === 'string' && (raw.charAt(0) === 'M' || raw.charAt(0) === 'm')) {
    ret.paths.push({ path: raw });
    return ret;
  }
  if (Array.isArray(raw)) {
    ret.paths = raw;
  } else if (typeof raw === 'object') {
    if (typeof raw.viewbox === 'number') {
      ret.viewbox = raw.viewbox;
    }
    if (raw.paths) {
      ret.paths = Array.isArray(raw.paths) ? raw.paths : [raw.paths];
    }
  }
  ret.paths = ret.paths
    .map((path: any) => {
      if (typeof path === 'string' && path.charAt(0) === 'M') {
        return { path };
      } else if (typeof path === 'object' && path.path) {
        return path;
      } else {
        return {};
      }
    })
    .filter((p) => p.path != null);
  return ret;
}
