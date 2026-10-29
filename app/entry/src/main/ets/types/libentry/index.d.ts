export const ping: () => string;
export const add: (a: number, b: number) => number;

declare const entry: {
  ping: () => string;
  add: (a: number, b: number) => number;
};

export default entry;
