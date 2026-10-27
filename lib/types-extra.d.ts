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
