export interface DecodedProfile {
  city: string;
  age: number;
}

export interface DecodedUser {
  /** int64, decimal string to preserve precision. */
  id: string;
  name: string;
  tags: string[];
  profile: DecodedProfile | null;
  email?: string;
  phone?: string;
  level?: number;
  /** JS-object semantics: duplicate wire keys resolve last-wins. */
  scores: Record<string, number>;
}

export const ping: () => string;
export const add: (a: number, b: number) => number;
export const decodeUser: (bytes: Uint8Array) => DecodedUser;

declare const entry: {
  ping: () => string;
  add: (a: number, b: number) => number;
  decodeUser: (bytes: Uint8Array) => DecodedUser;
};

export default entry;
