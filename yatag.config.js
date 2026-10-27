module.exports = {
  header: 'import * as stream from \'stream\';',
  include: [ 'src/**/*.@(cpp|hpp|h|c)', 'lib/*.js' ],
  output: 'lib/index.d.ts',
  root: 'gdal',
  // yatag drops the brackets of a member name, so `[Symbol.dispose]` would come
  // out as `Symbol.dispose()`, which is not valid TypeScript. The members which
  // are named by a symbol are added by the footer below instead
  filter: (name) => !name.match(/options\./g) && !name.match(/^\[Symbol\./),
  footer: `
export interface Dataset {
  [Symbol.dispose](): void
  [Symbol.asyncDispose](): Promise<void>
}`,
}
