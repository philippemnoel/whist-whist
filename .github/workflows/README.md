# Whist GitHub Actions Workflows

This subfolder contains the YAML workflow files for our GitHub Actions workflows. These workflows are integral to our continuous integration, handling everything from tests to building and deployment to cron jobs for cleanup and analysis.

## Syntax

The syntax for workflows is documented in the [GitHub Docs](https://docs.github.com/en/free-pro-team@latest/actions/reference/workflow-syntax-for-github-actions).

## Conventions

### Filenames

Since GitHub does not yet allow us to sort our workflow files into directories, we must name them in a clear and consistent manner. In particular, we name our workflows as `[subproject]-[verb]-[noun].yml`.

For example, a workflow for the `backend/services` projects which checks the PR and runs tests is called `backend-services-check-pr.yml`, whereas a meta workflow (one which operates on workflows and PRs themselves) which labels pull requests is named `meta-label-pull-requests.yml`. Workflows that belong to the whole repo -- for example, for pushing Sentry releases, are instead written `whist-deploy-sentry-releases.yml`.

### Headers

We used to have a convention which involved duplicating information and writing descriptions about workflows in comments atop the YAML files. This information is often not useful and just leads to cruft, so it is no longer required unless something particularly confusing needs to be documented.

### Jobs

Many of our workflows have a single job, whereas some of our more complex workflows (for example, `whist-publish-build.yml`) will contain multiple jobs triggered by the same event.

We used to have a convention that jobs needed to be named uniquely across workflows. This was extra caution on Roshan's part since we were not yet sure how GitHub would build out and stabilize the Actions API. While you may choose to use this convention, feel free to opt for concision and clarity and to avoid extra-long job names.

In many cases, `jobname` can simply be `main` -- for example, here is the start of the job description in `protocol-linting.yml`:

```yaml
jobs:
  protocol-linting-main:
    name: Lint Protocol
    runs-on: ubuntu-20.04
```

Notice that we also include a `Title Case`, plaintext name for the job, in addition to the tag `protocol-linting-main`.

In complex workflows with many jobs, please be specific in both the job identifier and job name. For example, in our deployments workflow, we might have a job to build the Banana service and upload to Beige's servers, among many other jobs. The YAML may look like:

```yaml
jobs:
  banana-build-publish-beige-server:
    name: "Banana: Build & Publish to Beige Server"
    runs-on: ubuntu-20.04
```

(Take this paragraph with a grain of salt. It is old and GitHub has probably fixed many of the early bugs.) Something to keep in mind when writing jobs, is that neither `cmd` nor `powershell` will fail if a command it runs fails. So, you should explicitly check if any commands you want to succeed, does indeed succeed. If you attach a job with `shell: bash` without specifying any arguments, then the job will _also_ not fail if any command it runs fails, unless it's the very last command (`cmd`/`powershell` will still not fail, even if the last command fails).

On macOS and Linux Ubuntu GitHub Actions runners, `bash --noprofile --norc -eo pipefail {0}` will be the default shell if you don't specify `shell:`, and so it the run command will fail if _any_ command fails. If you're specifying the `shell:` parameter yourself, make sure to add the relevant `-eo pipefail` arguments to ensure that it fails appropriately. On Windows, `cmd` will be the default shell, and failure needs to be checked for explicitly. For more information, see the [documentation](https://docs.github.com/en/actions/reference/workflow-syntax-for-github-actions#using-a-specific-shell).

### Styling

These YAML files are formatted with [Prettier](https://github.com/prettier/prettier). After installing, you can check formatting with `cd .github/workflows && prettier --check .`, and you can fix formatting with `cd .github/workflows && prettier --write .`. VSCode and other IDEs and text editors have pretty robust Prettier integration, so if you've set that up, that also works well!

## Testing

The easiest way to test a workflow is to enable it to be run manually -- to do this, make sure the repository's default branch version of the workflow contains the trigger

```yaml
on:
  workflow_dispatch:
```

Below this, you can optionally specify input parameters. Then, navigate to the page for the workflow in the Actions tab, where a button should appear allowing you to run the workflow manually.

## Contributing

Since we use `dev` as our head branch, there is usually no reason for workflows to be merged up to `prod` prematurely. Simply create your workflow, test it manually via `workflow_dispatch` or via setting it to run on `push` to your feature branch, and then PR it to `dev` as usual.

## Documentation

When writing a complicated workflow, or when you figure out how a confusing undocumented workflow works, please open a PR to `dev` documenting the details of that workflow below.
