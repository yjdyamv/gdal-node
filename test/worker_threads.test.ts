// gdal-async can be used from worker threads - each thread gets its own
// Node-API environment and its own object store

import * as gdal from 'gdal-async'
import { assert } from 'chai'
import { Worker } from 'worker_threads'
import * as path from 'path'

const ENTRY = path.resolve(__dirname, '..')
const SHP = `${__dirname}/data/cutline.shp`
const TIF = `${__dirname}/data/sample.tif`

// This code is evaluated inside the worker thread, so it has to be plain
// JavaScript and it cannot use any of the imports of this file
const workerCode = `
const { parentPort, workerData } = require('worker_threads')
const gdal = require(workerData.entry)

if (workerData.mode === 'async') {
  // keep asynchronous operations in flight until the thread is terminated
  const ds = gdal.open(workerData.tif)
  const band = ds.bands.get(1)
  const run = async () => {
    for (let i = 0; i < 1000; i++) {
      await band.pixels.readAsync(0, 0, 8, 8)
      parentPort.postMessage({ tick: i })
    }
  }
  run().catch((e) => parentPort.postMessage({ error: String(e) }))
} else {
  const ds = gdal.open(workerData.shp)
  const layer = ds.layers.get(0)
  const result = {
    drivers: gdal.drivers.count(),
    layers: ds.layers.count(),
    features: layer.features.count(),
    srs: ds.srs !== null,
    dataset: ds._uid,
    layer: layer._uid,
    foreign: workerData.foreign.map((uid) => gdal._isAlive(uid))
  }
  ds.close()
  parentPort.postMessage({ result })
}
`

interface WorkerResult {
  drivers: number
  layers: number
  features: number
  srs: boolean
  dataset: number
  layer: number
  foreign: boolean[]
}

interface WorkerMessage {
  result?: WorkerResult
  tick?: number
  error?: string
}

const startWorker = (workerData: object) => new Worker(workerCode, { eval: true, workerData })

const exit = (worker: Worker) => new Promise<void>((resolve) => {
  worker.on('exit', () => resolve())
})

describe('worker_threads', () => {
  it('should be able to use the addon in a worker thread', async () => {
    const worker = startWorker({ entry: ENTRY, shp: SHP, mode: 'open', foreign: [] })
    const result: WorkerResult = await new Promise((resolve, reject) => {
      worker.on('message', (message: WorkerMessage) => {
        if (message.error !== undefined) reject(new Error(message.error))
        else if (message.result !== undefined) resolve(message.result)
      })
      worker.on('error', reject)
    })
    await exit(worker)

    assert.isAbove(result.drivers, 0)
    assert.equal(result.layers, 1)
    assert.equal(result.features, 1)
    assert.isTrue(result.srs)

    // the main thread is still usable
    const ds = gdal.open(SHP)
    assert.equal(ds.layers.count(), 1)
    ds.close()
  })

  it('should not share objects between the main thread and the workers', async () => {
    const ds = gdal.open(SHP)
    // @ts-expect-error not a public API
    const dataset: number = ds._uid
    // @ts-expect-error not a public API
    const layer: number = ds.layers.get(0)._uid

    const worker = startWorker({
      entry: ENTRY, shp: SHP, mode: 'open', foreign: [ dataset, layer ]
    })
    const result: WorkerResult = await new Promise((resolve, reject) => {
      worker.on('message', (message: WorkerMessage) => {
        if (message.error !== undefined) reject(new Error(message.error))
        else if (message.result !== undefined) resolve(message.result)
      })
      worker.on('error', reject)
    })
    await exit(worker)

    // the objects of the main thread are not alive in the worker
    assert.deepEqual(result.foreign, [ false, false ])

    // the objects of the worker are not alive in the main thread
    // @ts-expect-error not a public API
    assert.isFalse(gdal._isAlive(result.dataset))
    // @ts-expect-error not a public API
    assert.isFalse(gdal._isAlive(result.layer))

    // but the objects of the main thread are still alive
    // @ts-expect-error not a public API
    assert.isTrue(gdal._isAlive(dataset))
    ds.close()
  })

  it('should survive the termination of a worker with in-flight asynchronous operations', async () => {
    const worker = startWorker({ entry: ENTRY, tif: TIF, mode: 'async' })
    await new Promise<void>((resolve, reject) => {
      worker.on('message', (message: WorkerMessage) => {
        if (message.error !== undefined) reject(new Error(message.error))
        else if (message.tick !== undefined && message.tick >= 5) resolve()
      })
      worker.on('error', reject)
    })
    // the worker is terminated while an asynchronous operation is running
    const code = await worker.terminate()
    assert.equal(code, 1)

    // the main thread is still usable
    const ds = gdal.open(SHP)
    assert.equal(ds.layers.count(), 1)
    ds.close()
  })
})
