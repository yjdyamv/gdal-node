module.exports = {
  header: 'import * as stream from \'stream\';',
  include: [ 'src/**/*.@(cpp|hpp|h|c)', 'lib/*.js' ],
  // the declarations which cannot be derived from the comments are written by
  // hand in lib/types-extra.d.ts, and scripts/generate-typings.js appends them
  // to this output
  // only scripts/generate-typings.js writes lib/index.d.ts: it is the file which
  // also carries lib/types-extra.d.ts, which a bare `npx yatag` would drop
  output: process.env.YATAG_OUTPUT || 'lib/index.generated.d.ts',
  root: 'gdal',
  // yatag drops the brackets of a member name, so `[Symbol.dispose]` would come
  // out as `Symbol.dispose()`, which is not valid TypeScript; the members which
  // are named by a symbol are declared in lib/types-extra.d.ts instead
  filter: (name) => !name.match(/options\./g) && !name.match(/^\[Symbol\./),
}
