import { promises as fs } from 'node:fs'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'
import * as child_process from 'node:child_process'
import { promisify } from 'node:util'
import { Octokit } from '@octokit/core'
const exec = promisify(child_process.exec)
const octokit = new Octokit({ auth: process.env.NODE_PRE_GYP_GITHUB_TOKEN })
const pkgJSON = JSON.parse(await fs.readFile(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', 'package.json')))
const version = pkgJSON.version
// The releases are hosted by the repository the binaries are downloaded from,
// the `host` of scripts/install.mjs, which is not necessarily the upstream one
const [, owner, repo] = pkgJSON.binary.host.match(/^https:\/\/github\.com\/([^/]+)\/([^/]+)\//) || []
if (!owner || !repo) throw new Error(`cannot tell the repository of ${pkgJSON.binary.host}`)
const pkg = { repo, owner }
// The numeric id of a workflow is local to the repository it was created in, so
// it cannot be hardcoded: look the workflow up by the file name instead
const workflows = (await octokit.request('GET /repos/{owner}/{repo}/actions/workflows', { ...pkg })).data.workflows
const workflow = workflows.find((w) => w.path === '.github/workflows/publish.yml')
if (!workflow) throw new Error(`${owner}/${repo} has no .github/workflows/publish.yml`)
const workflowPublishId = workflow.id
const workflowPublish = { ...pkg, workflow_id: workflowPublishId, ref: `v${version}` }

try {
  const branch = (await exec('git branch --show-current')).stdout.trim()
  process.stdout.write(`launching Github actions build on branch ${branch} for ${version}, tag ${workflowPublish.ref}`)
  await octokit.request('POST /repos/{owner}/{repo}/actions/workflows/{workflow_id}/dispatches', workflowPublish)

  let status
  do {
    process.stdout.write('.')
    // eslint-disable-next-line no-await-in-loop
    await new Promise((res) => setTimeout(res, 5000))
    // eslint-disable-next-line no-await-in-loop
    status = await octokit.request(`GET /repos/{owner}/{repo}/actions/workflows/${workflowPublishId}/runs`,
      { ...pkg, per_page: 1, page: 0 })
  } while (status.data.workflow_runs[0].status !== 'completed')
  process.stdout.write('\n')
  if (status.data.workflow_runs[0].conclusion !== 'success') {
    const url = new URL(status.data.workflow_runs[0].jobs_url)
    if (url.protocol !== 'https:' || url.hostname !== 'api.github.com' || !url.pathname.match(/^[a-z0-9/-]+$/g)) {
      throw new Error(`unexpected protocol URL ${url}`)
    }
    const jobs = await octokit.request(url.toString())
    const failedJob = jobs.data.jobs.filter((j) => ![ 'success', 'cancelled' ].includes(j.conclusion))
    if (failedJob[0]) {
      const failedStep = failedJob[0].steps.filter((j) => ![ 'success', 'cancelled' ].includes(j.conclusion))
      console.error('failed job', failedJob[0].name, failedStep[0].name)
    } else {
      console.error('job cancelled')
    }
    throw new Error('Github actions build failed')
  }
  process.stdout.write('success')
} catch (e) {
  console.error(e)
  process.exit(1)
}
