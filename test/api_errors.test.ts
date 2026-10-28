import gdal from 'gdal-node'
import { assert } from 'chai'
import * as path from 'path'

const fixture = (name: string): string => path.join(__dirname, 'data', name)

type Failure = Error & { code?: string }

// The failure of a call which has to throw
const thrown = (call: () => unknown): Failure => {
  try {
    call()
  } catch (e) {
    return e as Failure
  }
  throw new Error('should have thrown')
}

// The failure of an operation which has to reject
const rejected = async (operation: Promise<unknown>): Promise<Failure> => {
  try {
    await operation
  } catch (e) {
    return e as Failure
  }
  throw new Error('should have rejected')
}

describe('error classes', () => {
  it('should expose the classes', () => {
    const names = [
      'GDALError',
      'GDALOutOfMemoryError',
      'GDALFileIOError',
      'GDALOpenError',
      'GDALIllegalArgumentError',
      'GDALUnsupportedError',
      'GDALAssertionError',
      'GDALNoWriteAccessError',
      'GDALUserInterruptError',
      'GDALObjectNullError',
      'GDALHttpError',
      'GDALAWSError',
      'AbortError'
    ]
    for (const name of names) {
      assert.isFunction((gdal as unknown as Record<string, unknown>)[name], name)
    }
    assert.isTrue(gdal.AbortError.prototype instanceof gdal.GDALError)
    assert.isTrue(gdal.GDALError.prototype instanceof Error)
  })

  it('should build an error with the name and the code of its class', () => {
    const error = new gdal.GDALOpenError('it did not work')
    assert.equal(error.message, 'it did not work')
    assert.equal(error.name, 'GDALOpenError')
    assert.equal(error.code, 'CPLE_OpenFailed')
    assert.instanceOf(error, gdal.GDALError)
    assert.instanceOf(error, Error)
    assert.isString(error.stack)
  })

  it('should report a file which is not there as a GDALOpenError', () => {
    const error = thrown(() => gdal.open(fixture('does_not_exist.tif')))
    assert.instanceOf(error, gdal.GDALError)
    assert.instanceOf(error, gdal.GDALOpenError)
    assert.equal(error.name, 'GDALOpenError')
    assert.equal(error.code, 'CPLE_OpenFailed')
  })

  it('should report a file which is not there as a GDALOpenError when opening it asynchronously', async () => {
    const error = await rejected(gdal.openAsync(fixture('does_not_exist.tif')))
    assert.instanceOf(error, gdal.GDALOpenError)
    assert.equal(error.code, 'CPLE_OpenFailed')
  })

  it('should have a class for every CPLE error number', () => {
    // the numbers of the object storage failures are not exported
    const codes: [number, string, string][] = [
      [ gdal.CPLE_AppDefined, 'GDALError', 'CPLE_AppDefined' ],
      [ gdal.CPLE_OutOfMemory, 'GDALOutOfMemoryError', 'CPLE_OutOfMemory' ],
      [ gdal.CPLE_FileIO, 'GDALFileIOError', 'CPLE_FileIO' ],
      [ gdal.CPLE_OpenFailed, 'GDALOpenError', 'CPLE_OpenFailed' ],
      [ gdal.CPLE_IllegalArg, 'GDALIllegalArgumentError', 'CPLE_IllegalArg' ],
      [ gdal.CPLE_NotSupported, 'GDALUnsupportedError', 'CPLE_NotSupported' ],
      [ gdal.CPLE_AssertionFailed, 'GDALAssertionError', 'CPLE_AssertionFailed' ],
      [ gdal.CPLE_NoWriteAccess, 'GDALNoWriteAccessError', 'CPLE_NoWriteAccess' ],
      [ gdal.CPLE_UserInterrupt, 'GDALUserInterruptError', 'CPLE_UserInterrupt' ],
      [ 10, 'GDALObjectNullError', 'CPLE_ObjectNull' ],
      [ 11, 'GDALHttpError', 'CPLE_HttpResponse' ],
      [ 12, 'GDALAWSError', 'CPLE_AWSBucketNotFound' ],
      [ 13, 'GDALAWSError', 'CPLE_AWSObjectNotFound' ],
      [ 14, 'GDALAWSError', 'CPLE_AWSAccessDenied' ],
      [ 15, 'GDALAWSError', 'CPLE_AWSInvalidCredentials' ],
      [ 16, 'GDALAWSError', 'CPLE_AWSSignatureDoesNotMatch' ],
      [ 17, 'GDALAWSError', 'CPLE_AWSError' ]
    ]

    for (const [ code, name, errorCode ] of codes) {
      // @ts-expect-error not a public API
      const error = thrown(() => gdal._triggerCPLError(code, true))
      assert.instanceOf(error, gdal.GDALError, name)
      assert.equal(error.name, name, String(code))
      assert.equal(error.code, errorCode, String(code))
    }
  })

  it('should keep the errors of a bad argument a TypeError', () => {
    const dataset = gdal.open(fixture('sample.tif'))
    const error = thrown(() => (dataset.bands.get as (id: unknown) => unknown)('not a number'))
    assert.instanceOf(error, TypeError)
    assert.notInstanceOf(error, gdal.GDALError)
    dataset.close()
  })

  it('should report an interrupted operation as an AbortError', async () => {
    const dataset = gdal.open(fixture('sample.tif'))
    const controller = new AbortController()
    controller.abort()
    const error = await rejected(
      dataset.bands.get(1).computeStatisticsAsync(true, { abort_signal: controller.signal })
    )
    assert.instanceOf(error, gdal.AbortError)
    assert.instanceOf(error, gdal.GDALError)
    assert.equal(error.name, 'AbortError')
    assert.equal(error.code, 'ABORT_ERR')
    dataset.close()
  })

  it('should keep the class of a failure of a plain method', () => {
    // `Driver.deleteDataset` is not a body struct: it is a plain method, and
    // its failure used to arrive as a generic Error because the trampoline of
    // the methods did not know about GDALError
    const driver = gdal.drivers.get('GTiff') as gdal.Driver
    const error = thrown(() => driver.deleteDataset(fixture('does_not_exist.tif')))
    assert.instanceOf(error, gdal.GDALOpenError)
    assert.equal(error.code, 'CPLE_OpenFailed')
  })

  it('should keep the class of a failure of a static method and of a constructor', () => {
    assert.instanceOf(thrown(() => gdal.SpatialReference.fromWKT('not a wkt')), gdal.GDALError)
    assert.instanceOf(thrown(() => new gdal.SpatialReference('not a wkt')), gdal.GDALError)
  })
})
