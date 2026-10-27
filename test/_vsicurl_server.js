// The server behind test/open_vsicurl.test.ts
//
// It runs in a separate process on purpose: /vsicurl reads with synchronous
// HTTP requests when the method which triggered the read is synchronous, so an
// HTTP server which shares the event loop with the test would deadlock - it
// cannot answer while the event loop is blocked inside gdal.open().
//
// Usage: node test/_vsicurl_server.js <directory>
// It prints `PORT <port>` on stdout once it is listening.

const http = require('http')
const fs = require('fs')
const path = require('path')
const zlib = require('zlib')

const root = path.resolve(process.argv[2] || path.join(__dirname, 'data'))

// the chained test compresses the file on the fly, so that a single copy of the
// data serves both of them
const body = (name) => {
  const file = path.resolve(root, name.endsWith('.gz') ? name.slice(0, -3) : name)
  if (!file.startsWith(root + path.sep) || !fs.existsSync(file)) return null
  const data = fs.readFileSync(file)
  return name.endsWith('.gz') ? zlib.gzipSync(data) : data
}

const server = http.createServer((req, res) => {
  const name = decodeURIComponent((req.url || '/').replace(/^\/+/, '').replace(/\?.*$/, ''))
  const data = body(name)
  if (!data) {
    res.writeHead(404)
    res.end()
    return
  }

  const size = data.length
  const headers = { 'Accept-Ranges': 'bytes', 'Content-Type': 'application/octet-stream' }
  const range = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || '')

  if (!range) {
    res.writeHead(200, Object.assign({}, headers, { 'Content-Length': size }))
    res.end(req.method === 'HEAD' ? undefined : data)
    return
  }

  // /vsicurl always asks for the beginning of the file first and for the end of
  // it when it needs the IFD, so both of the open forms have to work
  const start = range[1] ? parseInt(range[1], 10) : Math.max(size - parseInt(range[2] || '0', 10), 0)
  const end = range[1] && range[2] ? parseInt(range[2], 10) : size - 1
  if (start >= size || start > end) {
    res.writeHead(416, Object.assign({}, headers, { 'Content-Range': `bytes */${size}` }))
    res.end()
    return
  }

  res.writeHead(
    206,
    Object.assign({}, headers, {
      'Content-Range': `bytes ${start}-${end}/${size}`,
      'Content-Length': end - start + 1
    })
  )
  res.end(req.method === 'HEAD' ? undefined : data.subarray(start, end + 1))
})

server.listen(0, '127.0.0.1', () => console.log(`PORT ${server.address().port}`))
