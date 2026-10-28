// Declarations which cannot be derived from the doc comments of the sources.
//
// This file is appended to lib/index.d.ts by scripts/generate-typings.js, see
// the comment at the top of that script for why these two have to be written by
// hand. Everything in here is documentation for typedoc and for the users.

// A dataset closes itself when an explicit resource management block ends.
// yatag drops the brackets of a member name, so `@method [Symbol.dispose]` would
// come out as `Symbol.dispose()`, which is not valid TypeScript
export interface Dataset {
  [Symbol.dispose](): void
  [Symbol.asyncDispose](): Promise<void>
}

/**
 * A failure of an operation.
 *
 * GDAL reports its failures with an error code and a message in a thread-local
 * buffer. Every failure which is not a mistake in the arguments is reported as a
 * GDALError or as one of its subclasses, one per CPLE error number, so that a
 * caller can tell what went wrong without matching the message:
 *
 *     try {
 *         gdal.open('does_not_exist.tif');
 *     } catch (e) {
 *         if (e instanceof gdal.GDALOpenError) console.error('the file is not there');
 *     }
 */
export class GDALError extends Error {
  /** The CPLE code of the failure, like CPLE_OpenFailed */
  code?: string
  constructor(message?: string, code?: string)
}
export class GDALOutOfMemoryError extends GDALError {}
/** A failure of a read or a write, CPLE_FileIO */
export class GDALFileIOError extends GDALError {}
/** A file or a data source which could not be opened, CPLE_OpenFailed */
export class GDALOpenError extends GDALError {}
/** An argument which GDAL refused, CPLE_IllegalArg */
export class GDALIllegalArgumentError extends GDALError {}
/** An operation which this build of GDAL cannot do, CPLE_NotSupported */
export class GDALUnsupportedError extends GDALError {}
export class GDALAssertionError extends GDALError {}
/** A read-only file or data source which was written to, CPLE_NoWriteAccess */
export class GDALNoWriteAccessError extends GDALError {}
export class GDALUserInterruptError extends GDALError {}
export class GDALObjectNullError extends GDALError {}
/** A failure of an HTTP request, CPLE_HttpResponse */
export class GDALHttpError extends GDALError {}
/** A failure of an object storage, the CPLE_AWS* codes of /vsicurl, /vsis3, /vsigs and the rest */
export class GDALAWSError extends GDALError {}
/** An operation which its AbortSignal interrupted, with the ABORT_ERR code of the Node.js core */
export class AbortError extends GDALError {
  code: 'ABORT_ERR'
}

// lib/gdal.js installs the band operators on top of gdal.algebra, so there is
// no registration which yatag could read them from
/**
 * The band operators, which apply `gdal.algebra` to the band.
 *
 * They are the equivalent of the arithmetic operators of JavaScript: they
 * return a new virtual band which computes the result when it is read.
 */
export interface RasterBand {
  abs(): RasterBand
  sqrt(): RasterBand
  log(): RasterBand
  log10(): RasterBand
  not(): RasterBand
  add(rhs: RasterBand|number): RasterBand
  sub(rhs: RasterBand|number): RasterBand
  mul(rhs: RasterBand|number): RasterBand
  div(rhs: RasterBand|number): RasterBand
  pow(rhs: RasterBand|number): RasterBand
  lt(rhs: RasterBand|number): RasterBand
  lte(rhs: RasterBand|number): RasterBand
  gt(rhs: RasterBand|number): RasterBand
  gte(rhs: RasterBand|number): RasterBand
  eq(rhs: RasterBand|number): RasterBand
  notEq(rhs: RasterBand|number): RasterBand
  and(rhs: RasterBand|number): RasterBand
  or(rhs: RasterBand|number): RasterBand
  min(rhs: RasterBand|number): RasterBand
  max(rhs: RasterBand|number): RasterBand
  mean(rhs: RasterBand|number): RasterBand
  ifThenElse(ifValue: RasterBand|number, elseValue: RasterBand|number): RasterBand
  asType(type: string): RasterBand
}

// the aliases of the multi geometry constructors, which lib/gdal.js sets on the
// single geometry constructors
export namespace Point {
  const Multi: typeof MultiPoint
}
export namespace LineString {
  const Multi: typeof MultiLineString
}
export namespace LinearRing {
  const Multi: typeof MultiLineString
}
export namespace Polygon {
  const Multi: typeof MultiPolygon
}

// the name which setPROJSearchPaths had before it was registered under the name
// given in its documentation
/**
 * Set paths where proj will search it data.
 *
 * @deprecated use `setPROJSearchPaths`, which this is an alias of
 */
export function setPROJSearchPath(path: string): void

// The scopes which open a dataset for the duration of a callback, which
// lib/gdal.js installs and which have no registration for yatag to read: they
// take the arguments of `gdal.open()` and a callback, or the arguments as an
// array - which is what a dataset that has to be created with a driver needs
export type OpenArguments =
  | [ path: string|Buffer, mode?: string ]
  | [ path: string|Buffer, mode: string, drivers: string|string[],
      x_size?: number, y_size?: number, band_count?: number,
      data_type?: string, creation_options?: string[]|object ]

/**
 * Opens a dataset and closes it when the callback returns
 *
 * The dataset is closed when the callback returns or throws, unless the
 * callback returns the dataset itself, which means that the caller takes it
 * over. A callback which returns a promise is allowed - the dataset is closed
 * when the promise settles - but `withDatasetAsync()`, which does not block the
 * event loop, is the one to use then.
 *
 * @example
 *
 * const count = gdal.withDataset('in.tif', 'r', (ds) => ds.bands.count())
 *
 * @example
 *
 * gdal.withDataset([ 'out.tif', 'w', 'GTiff', 400, 300, 1, gdal.GDT_Byte ], (ds) => {
 *   ds.bands.get(1).pixels.write(0, 0, 400, 300, new Uint8Array(120000))
 * })
 *
 * @throws {TypeError} if the last argument is not a function
 */
export function withDataset<T>(args: OpenArguments, fn: (dataset: Dataset) => T): T
export function withDataset<T>(path: string|Buffer, mode: string, fn: (dataset: Dataset) => T): T

/**
 * Opens a dataset and closes it when the callback finishes
 *
 * The same as `withDataset()`, with the dataset opened and closed without
 * blocking the event loop: the promise of the call resolves with what the
 * callback returned, after the dataset has been closed.
 *
 * @example
 *
 * const count = await gdal.withDatasetAsync('in.tif', 'r', (ds) => ds.bands.countAsync())
 *
 * @throws {TypeError} if the last argument is not a function
 */
export function withDatasetAsync<T>(args: OpenArguments, fn: (dataset: Dataset) => T|Promise<T>): Promise<T>
export function withDatasetAsync<T>(path: string|Buffer, mode: string, fn: (dataset: Dataset) => T|Promise<T>): Promise<T>
