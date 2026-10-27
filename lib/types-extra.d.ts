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
