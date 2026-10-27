import * as child_process from 'child_process'
import type { ChildProcess } from 'child_process'
import * as path from 'path'

// Serves the files of `test/data` over HTTP with `test/_vsicurl_server.js`, for
// the tests which need a URL and do not want to depend on the network.
//
// The server runs in its own process: a synchronous read through `/vsicurl` - or
// a synchronous `SpatialReference.fromURL` - blocks the event loop of the test,
// so a server which shares it could never answer.
export function startServer(
  directory = path.join(__dirname, '..', 'data')
): Promise<{ base: string, stop: () => void }> {
  const server: ChildProcess = child_process.spawn(
    process.execPath,
    [ path.join(__dirname, '..', '_vsicurl_server.js'), directory ],
    { stdio: [ 'ignore', 'pipe', 'inherit' ] }
  )
  return new Promise((resolve, reject) => {
    const onError = (err: Error) => reject(err)
    server.on('error', onError)
    server.stdout!.setEncoding('utf8')
    server.stdout!.on('data', (chunk: string) => {
      const port = /PORT (\d+)/.exec(chunk)
      if (!port) return
      server.stdout!.removeAllListeners('data')
      server.removeListener('error', onError)
      resolve({ base: `http://127.0.0.1:${port[1]}`, stop: () => server.kill() })
    })
  })
}
